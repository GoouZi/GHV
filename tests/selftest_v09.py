#!/usr/bin/env python3
from __future__ import annotations
import os, shutil, struct, subprocess, sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from ghv.container import read_header,read_index,FRAME_FMT,FRAME_SIZE,VFRM

def main():
    src=ROOT/'tests'/'sample.mp4';dec=ROOT/'native'/'bin'/('ghvdecode.exe' if os.name=='nt' else 'ghvdecode')
    out=ROOT/'tests'/'_selftest_v09.ghv';bad=ROOT/'tests'/'_selftest_v09_bad.ghv';repaired=ROOT/'tests'/'_selftest_v09_repaired.ghv'
    try:
        p=subprocess.run([sys.executable,str(ROOT/'ghvenc.py'),str(src),str(out),'--preset','balanced','--native','on','--codec','9','--no-audio'],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
        if p.returncode:print(p.stdout);raise RuntimeError('GHVC9 encode failed')
        first_p=None
        with open(out,'rb') as f:
            h=read_header(f);idx=read_index(f,h);assert h.major==0 and h.minor==9 and h.frame_count==len(idx)>1
            for off,_ in idx:
                f.seek(off);v=struct.unpack(FRAME_FMT,f.read(FRAME_SIZE));assert v[0]==VFRM and v[4]==9
                payload=f.read(v[7])
                if v[3]==0:assert payload[:4]==b'GTC7'
                elif v[3]==1:
                    assert payload[:4] in (b'GBP9',b'GTP9');first_p=(off,v,payload);break
        assert first_p is not None
        print(f'[PASS] GHVC9 encode/container and chunked coefficient syntax: {h.frame_count} frames')
        p=subprocess.run([str(dec),str(out),'--verify','--no-output'],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace')
        if p.returncode:raise RuntimeError('GHVC9 native decode/CRC failed: '+p.stderr)
        print('[PASS] GHVC9 native decode + CRC')
        shutil.copyfile(out,bad)
        off,v,payload=first_p
        if payload[:4]==b'GBP9':
            mvbytes=struct.unpack_from('<I',payload,20)[0];blocks=struct.unpack_from('<I',payload,12)[0]
            chunk_table=off+FRAME_SIZE+24+mvbytes+(blocks+7)//8
            with open(bad,'r+b') as f:f.seek(chunk_table);f.write(struct.pack('<I',0xffffffff))
            q=subprocess.run([str(dec),str(bad),'--verify','--no-output'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            assert q.returncode!=0
            print('[PASS] GHVC9 malformed chunk table rejected safely')
        p=subprocess.run([sys.executable,str(ROOT/'ghvrepair.py'),str(out),str(repaired)],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
        if p.returncode:raise RuntimeError('GHVC9 repair failed: '+p.stdout)
        with open(repaired,'rb') as f:rh=read_header(f);ridx=read_index(f,rh);assert rh.minor==9 and len(ridx)==rh.frame_count
        print('[PASS] GHV 0.9 / GHVC9 self-test complete')
    finally:
        for path in (out,bad,repaired):
            try:path.unlink()
            except OSError:pass

if __name__=='__main__':main()
