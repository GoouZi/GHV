#!/usr/bin/env python3
from __future__ import annotations
import argparse, binascii, os, struct, subprocess, time
from pathlib import Path
from ghv.container import read_header, read_index, FRAME_FMT, FRAME_SIZE, VFRM, unpack_motion
from ghv.codec3 import decode_frame as decode3
from ghv.codec4 import decode_frame as decode4
from ghv.codec5 import decode_frame as decode5
from ghv.codec6 import decode_frame as decode6


def native_decoder():
    p = Path(__file__).resolve().parent / 'native' / 'bin' / ('ghvdecode.exe' if os.name == 'nt' else 'ghvdecode')
    return str(p) if p.is_file() else None


def verify_python(path: str):
    t0=time.perf_counter(); prev=None
    with open(path,'rb') as f:
        h=read_header(f); idx=read_index(f,h)
        if len(idx)!=h.frame_count: raise ValueError(f'index count {len(idx)} != header frame_count {h.frame_count}')
        last=-1
        for i,(off,itype) in enumerate(idx):
            if off<=last: raise ValueError(f'index offsets not increasing at frame {i}')
            last=off; f.seek(off); rh=f.read(FRAME_SIZE)
            if len(rh)!=FRAME_SIZE: raise ValueError(f'truncated frame header {i}')
            magic,no,pts,typ,codec,meta,raw_size,packed_size,checksum=struct.unpack(FRAME_FMT,rh)
            if magic!=VFRM or no!=i or typ!=itype: raise ValueError(f'frame/index mismatch at {i}')
            payload=f.read(packed_size)
            if len(payload)!=packed_size: raise ValueError(f'truncated payload {i}')
            dx,dy=unpack_motion(meta)
            if codec==6: y=decode6(typ,payload,prev,h.width,h.height,raw_size,dx,dy)
            elif codec==5: y=decode5(typ,payload,prev,h.width,h.height,raw_size,dx,dy)
            elif codec==4: y=decode4(typ,payload,prev,h.width,h.height,raw_size,dx,dy)
            elif codec==3: y=decode3(typ,payload,prev,h.width,h.height,raw_size,dx,dy)
            elif codec == 7: raise ValueError('GHVC7 Python reference decoder is not implemented; use the native verifier')
            else: raise ValueError(f'unsupported codec {codec} at frame {i}')
            if (binascii.crc32(y)&0xffffffff)!=checksum: raise ValueError(f'CRC mismatch at frame {i}')
            prev=y
    sec=max(1e-6,time.perf_counter()-t0)
    return h.frame_count,sec,h.frame_count/sec


def main():
    ap=argparse.ArgumentParser(description='Verify GHV index, frame decode and CRCs')
    ap.add_argument('input'); ap.add_argument('--python',action='store_true',help='force Python verifier')
    a=ap.parse_args()
    with open(a.input,'rb') as f:
        h=read_header(f); idx=read_index(f,h)
        codec=0
        if idx:
            f.seek(idx[0][0]); rh=f.read(FRAME_SIZE)
            if len(rh)==FRAME_SIZE: codec=struct.unpack(FRAME_FMT,rh)[4]
    nd=native_decoder()
    if not a.python and codec in (4,5,6,7,8) and nd:
        t0=time.perf_counter()
        p=subprocess.run([nd,a.input,'--verify'],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace')
        sec=max(1e-6,time.perf_counter()-t0)
        if p.returncode!=0:
            print(p.stderr.strip())
            raise SystemExit(f'[FAIL] native verification failed (exit {p.returncode})')
        print(f'[PASS] GHV {h.major}.{h.minor} / GHVC{codec} native verification: {h.frame_count} frames, {sec:.3f}s, {h.frame_count/sec:.1f} fps')
        return
    n,sec,fps=verify_python(a.input)
    print(f'[PASS] GHV {h.major}.{h.minor} Python verification: {n} frames, {sec:.3f}s, {fps:.1f} fps')

if __name__=='__main__': main()
