#!/usr/bin/env python3
from __future__ import annotations
import argparse, os, re, shutil, struct, subprocess, time
from pathlib import Path
from ghv.container import read_header, read_index, FRAME_FMT, FRAME_SIZE


def native_decoder():
    p = Path(__file__).resolve().parent / 'native' / 'bin' / ('ghvdecode.exe' if os.name == 'nt' else 'ghvdecode')
    return str(p) if p.is_file() else None



def ffmpeg_tool() -> str | None:
    p = shutil.which('ffmpeg')
    if p:
        return p
    if os.name == 'nt':
        for q in (r'E:\ffmpeg\bin\ffmpeg.exe', r'C:\ffmpeg\bin\ffmpeg.exe'):
            if os.path.isfile(q):
                return q
    return None


def benchmark_pipe(nd: str, path: str, w: int, h: int, fps_num: int, fps_den: int, n: int, verify: bool):
    ffmpeg = ffmpeg_tool()
    if not ffmpeg:
        return None
    dcmd = [nd, path, '--frames', str(n), '--buffer-frames', '24']
    if verify:
        dcmd.append('--verify')
    fcmd = [ffmpeg, '-hide_banner', '-loglevel', 'error',
            '-f', 'rawvideo', '-pixel_format', 'yuv420p',
            '-video_size', f'{w}x{h}', '-framerate', f'{fps_num}/{fps_den}',
            '-i', 'pipe:0', '-map', '0:v:0', '-f', 'null', '-']
    t0 = time.perf_counter()
    dec = subprocess.Popen(dcmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                           bufsize=16 * 1024 * 1024)
    assert dec.stdout is not None
    ff = subprocess.Popen(fcmd, stdin=dec.stdout, stdout=subprocess.DEVNULL,
                          stderr=subprocess.PIPE, bufsize=16 * 1024 * 1024)
    dec.stdout.close()
    _, ferr = ff.communicate()
    drc = dec.wait()
    wall = max(1e-6, time.perf_counter() - t0)
    if drc != 0 or ff.returncode != 0:
        msg = (ferr or b'').decode('utf-8', 'replace')
        return ('error', drc, ff.returncode, msg)
    return ('ok', n / wall, wall)

def main():
    ap = argparse.ArgumentParser(description='GHV playback/decoder diagnostics')
    ap.add_argument('input')
    ap.add_argument('--frames', type=int, default=180, help='frames to benchmark, default 180')
    ap.add_argument('--verify', action='store_true')
    a = ap.parse_args()
    with open(a.input, 'rb') as f:
        h = read_header(f); idx = read_index(f, h)
        codec = 0; video_payload = 0
        for i,(off,_) in enumerate(idx):
            f.seek(off); rh=f.read(FRAME_SIZE)
            if len(rh)!=FRAME_SIZE: break
            vals=struct.unpack(FRAME_FMT,rh); codec = vals[4] if i==0 else codec
            video_payload += FRAME_SIZE + vals[7]
    fps = h.fps_num / h.fps_den
    raw_mib_s = h.width*h.height*1.5*fps/(1024*1024)
    duration = h.duration_us/1e6 if h.duration_us else (h.frame_count/fps if fps else 0)
    file_mib = os.path.getsize(a.input)/(1024*1024)
    bitrate = (os.path.getsize(a.input)*8/duration/1e6) if duration else 0
    print(f'GHV {h.major}.{h.minor} / GHVC{codec}')
    print(f'{h.width}x{h.height} @ {fps:.3f} fps | {h.frame_count} frames | {duration:.2f}s')
    print(f'File: {file_mib:.2f} MiB | average bitrate: {bitrate:.2f} Mbit/s')
    print(f'Uncompressed playback pipe: {raw_mib_s:.1f} MiB/s')
    nd = native_decoder()
    if not nd:
        print('Native decoder: NOT FOUND')
        print('Result: Python playback is likely to struggle with HD video. Build native/ghvdecode first.')
        return 2
    n=max(1,min(h.frame_count,a.frames))
    cmd=[nd,a.input,'--no-output','--frames',str(n)]
    if a.verify: cmd.append('--verify')
    t0=time.perf_counter(); p=subprocess.run(cmd,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace'); wall=time.perf_counter()-t0
    if p.returncode:
        print(p.stderr.strip()); print(f'Result: decoder failed with exit {p.returncode}')
        return p.returncode
    m=re.search(r'fps=([0-9.]+)',p.stderr)
    dfps=float(m.group(1)) if m else n/max(wall,1e-6)
    headroom=dfps/max(fps,1e-6)
    print(f'Native decode: {dfps:.1f} fps ({headroom:.2f}x realtime) over {n} frames')
    pipe = benchmark_pipe(nd, a.input, h.width, h.height, h.fps_num, h.fps_den, n, a.verify)
    if pipe:
        if pipe[0] == 'ok':
            pfps = pipe[1]; phead = pfps / max(fps, 1e-6)
            print(f'Native->FFmpeg pipe: {pfps:.1f} fps ({phead:.2f}x realtime)')
            if phead < 1.15:
                print('Pipeline diagnosis: TOO SLOW — presentation pipe, not only the codec, is a bottleneck.')
            elif phead < 1.8:
                print('Pipeline diagnosis: BORDERLINE — use Auto/24+ frame playback buffer for HD.')
            else:
                print('Pipeline diagnosis: GOOD — decode + rawvideo handoff has realtime headroom.')
        else:
            print(f'Pipeline benchmark failed: decoder={pipe[1]} ffmpeg={pipe[2]} {pipe[3]}')
    if headroom >= 1.8:
        print('Playback diagnosis: GOOD — decoder has comfortable realtime headroom.')
    elif headroom >= 1.15:
        print('Playback diagnosis: BORDERLINE — should play, but heavy scenes may stutter. Increase player buffer.')
    else:
        print('Playback diagnosis: TOO SLOW — decoder cannot reliably sustain realtime on this machine/file.')
    return 0

if __name__=='__main__':
    raise SystemExit(main())
