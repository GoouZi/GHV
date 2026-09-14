#!/usr/bin/env python3
from __future__ import annotations
import os, struct, subprocess, sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from ghv.container import read_header,read_index,FRAME_FMT,FRAME_SIZE,VFRM

def main():
    src=ROOT/'tests'/'sample.mp4';dec=ROOT/'native'/'bin'/('ghvdecode.exe' if os.name=='nt' else 'ghvdecode')
    if not src.exists() or not dec.exists():raise SystemExit('GHVC8 self-test requires sample.mp4 and built native tools')
    out=ROOT/'tests'/'_selftest_v08.ghv';repaired=ROOT/'tests'/'_selftest_v08_repaired.ghv'
    try:
        p=subprocess.run([sys.executable,str(ROOT/'ghvenc.py'),str(src),str(out),'--preset','balanced','--native','on','--codec','8'],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
        if p.returncode:print(p.stdout);raise RuntimeError('GHVC8 encode failed')
        with open(out,'rb') as f:
            h=read_header(f);idx=read_index(f,h);assert h.major==0 and h.minor==8 and h.frame_count==len(idx)>1
            f.seek(idx[0][0]);v=struct.unpack(FRAME_FMT,f.read(FRAME_SIZE));assert v[0]==VFRM and v[4]==8 and v[3]==0 and f.read(v[7])[:4]==b'GTC7'
            f.seek(idx[1][0]);v=struct.unpack(FRAME_FMT,f.read(FRAME_SIZE));assert v[4]==8 and v[3] in (1,2)
            if v[3]==1:assert f.read(v[7])[:4]==b'GTP8'
        print(f'[PASS] GHVC8 encode/container and versioned P syntax: {h.frame_count} frames')
        p=subprocess.run([str(dec),str(out),'--verify','--no-output'],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace')
        if p.returncode:raise RuntimeError('GHVC8 native decode/CRC failed: '+p.stderr)
        print('[PASS] GHVC8 native decode + CRC')
        p=subprocess.run([sys.executable,str(ROOT/'ghvrepair.py'),str(out),str(repaired)],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
        if p.returncode:raise RuntimeError('GHVC8 repair failed: '+p.stdout)
        with open(repaired,'rb') as f:rh=read_header(f);ridx=read_index(f,rh);assert rh.minor==8 and len(ridx)==rh.frame_count
        print('[PASS] GHV 0.8 / GHVC8 self-test complete')
    finally:
        for path in (out,repaired):
            try:path.unlink()
            except OSError:pass

if __name__=='__main__':main()
