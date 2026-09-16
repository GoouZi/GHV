#!/usr/bin/env python3
"""Extract fixed 10/25/50/75/90% source/GHV visual comparison frames."""
from __future__ import annotations
import argparse,os,shutil,subprocess,tempfile
from pathlib import Path
import numpy as np
from ghv.container import read_header
from ghv.paths import native_binary

POINTS=(.10,.25,.50,.75,.90)

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('source');ap.add_argument('ghv');ap.add_argument('output_dir');a=ap.parse_args()
    try:import cv2
    except ImportError:raise SystemExit('OpenCV is required')
    source=Path(a.source).resolve();ghv=Path(a.ghv).resolve();out=Path(a.output_dir).resolve();out.mkdir(parents=True,exist_ok=True)
    with open(ghv,'rb') as f:h=read_header(f)
    targets=[min(h.frame_count-1,int(round((h.frame_count-1)*p))) for p in POINTS]
    ffmpeg=shutil.which('ffmpeg');dec=native_binary('ghvdecode')
    if not ffmpeg or not dec.is_file():raise SystemExit('ffmpeg and native ghvdecode are required')
    with tempfile.TemporaryDirectory(prefix='ghvframes_') as td:
        tmp=Path(td);expr='+'.join(f'eq(n\\,{n})' for n in targets)
        cmd=[ffmpeg,'-v','error','-i',str(source),'-vf',f'select={expr}','-vsync','0',str(tmp/'source_%02d.png')]
        subprocess.run(cmd,check=True)
        proc=subprocess.Popen([str(dec),str(ghv)],stdout=subprocess.PIPE,stderr=subprocess.PIPE);assert proc.stdout is not None
        decoded={};frame_bytes=h.width*h.height*3//2
        for n in range(targets[-1]+1):
            raw=proc.stdout.read(frame_bytes)
            if len(raw)!=frame_bytes:raise RuntimeError(f'truncated decoder output at frame {n}')
            if n in targets:
                yuv=np.frombuffer(raw,dtype=np.uint8).reshape(h.height*3//2,h.width)
                decoded[n]=cv2.cvtColor(yuv,cv2.COLOR_YUV2BGR_I420)
        proc.terminate();proc.wait(timeout=3)
        for i,(pct,n) in enumerate(zip(POINTS,targets),1):
            src=cv2.imread(str(tmp/f'source_{i:02d}.png'));dst=decoded[n]
            if src is None:raise RuntimeError(f'source extraction failed at frame {n}')
            if src.shape[:2]!=dst.shape[:2]:src=cv2.resize(src,(h.width,h.height),interpolation=cv2.INTER_AREA)
            canvas=np.hstack((src,dst));bar=max(36,h.height//24);canvas=cv2.copyMakeBorder(canvas,bar,0,0,0,cv2.BORDER_CONSTANT,value=(20,20,20))
            scale=max(.6,h.width/1600);cv2.putText(canvas,f'SOURCE  {int(pct*100)}%  frame {n}',(12,bar-10),cv2.FONT_HERSHEY_SIMPLEX,scale,(240,240,240),2,cv2.LINE_AA)
            cv2.putText(canvas,f'GHVC{h.minor} DECODED',(h.width+12,bar-10),cv2.FONT_HERSHEY_SIMPLEX,scale,(240,240,240),2,cv2.LINE_AA)
            path=out/f'compare_{int(pct*100):02d}_frame_{n}.png';cv2.imwrite(str(path),canvas)
    print(f'[GHV] Wrote {len(targets)} fixed visual comparisons to {out}')

if __name__=='__main__':main()
