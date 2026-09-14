#!/usr/bin/env python3
from __future__ import annotations
import argparse, binascii, os, queue, shutil, struct, subprocess, tempfile, threading, time, wave
from pathlib import Path
import numpy as np

from ghv.codec3 import decode_frame as decode_frame3
from ghv.codec4 import decode_frame as decode_frame4
from ghv.gha import decode_pcm16le as decode_ghac1
from ghv.container import (read_header, read_index, FRAME_FMT, FRAME_SIZE,
                           AUDIO_FMT, AUDIO_SIZE, VFRM, AUD0, unpack_motion)


def find_tool(name, explicit=None):
    if explicit:
        return explicit
    p = shutil.which(name)
    if p:
        return p
    if os.name == 'nt':
        for g in [rf'E:\ffmpeg\bin\{name}.exe', rf'C:\ffmpeg\bin\{name}.exe']:
            if os.path.exists(g):
                return g
    return None


def find_native_decoder():
    base = Path(__file__).resolve().parent / 'native' / 'bin'
    p = base / ('ghvdecode.exe' if os.name == 'nt' else 'ghvdecode')
    return str(p) if p.is_file() else None


def load_audio_to_wav(f, h, dest):
    if not (h.flags & 1) or not h.audio_offset:
        return False
    f.seek(h.audio_offset)
    raw = f.read(AUDIO_SIZE)
    if len(raw) != AUDIO_SIZE:
        raise ValueError('truncated GHV audio chunk')
    magic, codec, packed_size, samples = struct.unpack(AUDIO_FMT, raw)
    if magic != AUD0:
        raise ValueError('bad GHV audio chunk')
    data = f.read(packed_size)
    if len(data) != packed_size:
        raise ValueError('truncated GHV audio payload')
    if codec != 4:
        raise ValueError(f'unsupported embedded audio codec {codec}')
    pcm, sample_rate, channels, frames = decode_ghac1(data)
    if sample_rate != h.audio_rate or channels != h.audio_channels:
        raise ValueError('GHAC1 metadata mismatch')
    with wave.open(dest, 'wb') as wf:
        wf.setnchannels(channels); wf.setsampwidth(2); wf.setframerate(sample_rate); wf.writeframes(pcm)
    return True


def frame_codec_at(f, offset):
    f.seek(offset)
    fh = f.read(FRAME_SIZE)
    if len(fh) != FRAME_SIZE:
        raise ValueError('truncated first frame')
    vals = struct.unpack(FRAME_FMT, fh)
    if vals[0] != VFRM:
        raise ValueError('missing VFRM')
    return vals[4]


def decode_one(f, h, prev, verify=False):
    fh = f.read(FRAME_SIZE)
    if len(fh) != FRAME_SIZE:
        return None, None, None
    magic, no, pts, typ, codec, meta, raw_size, packed_size, checksum = struct.unpack(FRAME_FMT, fh)
    if magic != VFRM:
        raise ValueError(f'bad frame chunk #{no}')
    payload = f.read(packed_size)
    if len(payload) != packed_size:
        raise ValueError(f'truncated frame payload #{no}')
    dx, dy = unpack_motion(meta)
    if codec == 4:
        yuv = decode_frame4(typ, payload, prev, h.width, h.height, raw_size, dx, dy)
    elif codec == 3:
        yuv = decode_frame3(typ, payload, prev, h.width, h.height, raw_size, dx, dy)
    else:
        raise ValueError(f'unsupported GHV video codec {codec}')
    if verify and (binascii.crc32(yuv) & 0xFFFFFFFF) != checksum:
        raise ValueError(f'CRC mismatch at frame {no}')
    return yuv, no, pts


def yuv420_to_bgr_cv(yuv: bytes, w: int, h: int, cv2):
    a = np.frombuffer(yuv, dtype=np.uint8).reshape(h * 3 // 2, w)
    return cv2.cvtColor(a, cv2.COLOR_YUV2BGR_I420)


def play_native(args, h, fps, target_frame, wav_path):
    native = find_native_decoder()
    ffmpeg = find_tool('ffmpeg', args.ffmpeg)
    ffplay = find_tool('ffplay', args.ffplay)
    if not native or not ffmpeg or not ffplay:
        return False

    dec_cmd = [native, args.input, '--start-frame', str(target_frame)]
    if args.verify:
        dec_cmd.append('--verify')
    fps_expr = f'{h.fps_num}/{h.fps_den}'
    mux_cmd = [ffmpeg, '-hide_banner', '-loglevel', 'error',
               '-f', 'rawvideo', '-pixel_format', 'yuv420p', '-video_size', f'{h.width}x{h.height}',
               '-framerate', fps_expr, '-i', '-']
    if wav_path and not args.no_audio:
        mux_cmd += ['-ss', f'{args.start:.6f}', '-i', wav_path, '-map', '0:v:0', '-map', '1:a:0',
                    '-c:v', 'rawvideo', '-c:a', 'pcm_s16le']
    else:
        mux_cmd += ['-map', '0:v:0', '-c:v', 'rawvideo']
    mux_cmd += ['-f', 'nut', '-']
    play_cmd = [ffplay, '-hide_banner', '-loglevel', 'warning', '-autoexit', '-i', '-']

    print('[GHV] Playback engine: Native GHVC4 decoder + FFmpeg/ffplay presentation')
    derr = tempfile.TemporaryFile(mode='w+b'); merr = tempfile.TemporaryFile(mode='w+b')
    dec = mux = player = None
    try:
        dec = subprocess.Popen(dec_cmd, stdout=subprocess.PIPE, stderr=derr, bufsize=4 * 1024 * 1024)
        assert dec.stdout is not None
        mux = subprocess.Popen(mux_cmd, stdin=dec.stdout, stdout=subprocess.PIPE, stderr=merr, bufsize=4 * 1024 * 1024)
        dec.stdout.close()
        assert mux.stdout is not None
        player = subprocess.Popen(play_cmd, stdin=mux.stdout)
        mux.stdout.close()
        prc = player.wait()
        if mux.poll() is None:
            mux.terminate()
        mrc = mux.wait(timeout=3) if mux.poll() is None else mux.returncode
        if dec.poll() is None:
            dec.terminate()
        drc = dec.wait(timeout=3) if dec.poll() is None else dec.returncode
        if prc not in (0, 255):
            print(f'[GHV] ffplay exited with {prc}')
        if drc not in (0, -15, 1):
            derr.seek(0); print('[GHV] Native decoder:', derr.read().decode('utf-8', 'replace'))
        if mrc not in (0, -15, 1):
            merr.seek(0); print('[GHV] FFmpeg mux:', merr.read().decode('utf-8', 'replace'))
        return True
    finally:
        for p in (player, mux, dec):
            if p and p.poll() is None:
                try: p.terminate()
                except Exception: pass
        derr.close(); merr.close()


def play_python(args, h, idx, fps, target, start_frame, worker_offset, worker_prev, frame_no, wav_path):
    try:
        import cv2
    except ImportError:
        raise SystemExit('OpenCV required for Python playback: pip install opencv-python numpy')

    qsize = max(3, min(96, int(args.buffer)))
    dq: queue.Queue = queue.Queue(maxsize=qsize)
    stop = threading.Event()

    def decoder_worker():
        try:
            with open(args.input, 'rb') as df:
                df.seek(worker_offset); prev_local = worker_prev; n = frame_no
                while n < h.frame_count and not stop.is_set():
                    try:
                        yuv, no, pts = decode_one(df, h, prev_local, args.verify)
                        if yuv is None: break
                        prev_local = yuv
                        while not stop.is_set():
                            try: dq.put(('frame', yuv, no, pts), timeout=.25); break
                            except queue.Full: pass
                        n += 1
                    except Exception as exc:
                        if args.strict:
                            dq.put(('error', exc)); return
                        j = n + 1
                        while j < h.frame_count and idx[j][1] != 0: j += 1
                        if j >= h.frame_count:
                            dq.put(('error', exc)); return
                        dq.put(('warn', f'decode error at frame {n}: {exc}; recovering at keyframe {j}'))
                        df.seek(idx[j][0]); prev_local = None; n = j
                dq.put(('eof',))
        except Exception as exc:
            try: dq.put(('error', exc), timeout=.5)
            except Exception: pass

    th = threading.Thread(target=decoder_worker, daemon=True); th.start()
    prebuffer = min(max(6, int(fps * .25)), qsize)
    deadline = time.perf_counter() + 8.0
    while dq.qsize() < prebuffer and th.is_alive() and time.perf_counter() < deadline: time.sleep(.01)

    audio_proc = None
    if wav_path and not args.no_audio:
        ffplay = find_tool('ffplay', args.ffplay)
        if ffplay:
            audio_proc = subprocess.Popen([ffplay, '-nodisp', '-autoexit', '-loglevel', 'quiet', '-ss', f'{args.start:.6f}', wav_path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    print('[GHV] Playback engine: Python compatibility renderer')
    win = 'GHV 0.5 Player - Q/ESC to quit'
    cv2.namedWindow(win, cv2.WINDOW_NORMAL); cv2.resizeWindow(win, h.width, h.height)
    base = time.perf_counter(); target_pts_us = int(args.start * 1_000_000); dropped=shown=starves=0
    try:
        while True:
            try:
                item = dq.get(timeout=1.0)
            except queue.Empty:
                starves += 1
                if not th.is_alive(): raise RuntimeError('decoder stopped before EOF')
                if starves >= 3: print('[GHV] Warning: decoder cannot keep up; native playback is recommended.')
                continue
            tag=item[0]
            if tag=='eof': break
            if tag=='warn': print('[GHV] Warning:',item[1]); continue
            if tag=='error': raise item[1]
            _,yuv,no,pts=item
            due=max(0.0,(pts-target_pts_us)/1_000_000.0); now=time.perf_counter()-base; lateness=now-due
            if lateness>(1.25/max(fps,1e-6)) and not dq.empty(): dropped+=1; continue
            if due>now: time.sleep(due-now)
            bgr=yuv420_to_bgr_cv(yuv,h.width,h.height,cv2); cv2.imshow(win,bgr)
            key=cv2.waitKey(1)&0xFF
            if key in (27,ord('q'),ord('Q')): break
            shown+=1
    finally:
        stop.set(); th.join(timeout=1); cv2.destroyAllWindows()
        if audio_proc and audio_proc.poll() is None: audio_proc.terminate()
        print(f'[GHV] Playback ended. shown={shown} dropped={dropped} starvation_events={starves}')


def main():
    ap=argparse.ArgumentParser(description='GHV 0.5 reference player')
    ap.add_argument('input'); ap.add_argument('--info',action='store_true'); ap.add_argument('--start',type=float,default=0.0)
    ap.add_argument('--no-audio',action='store_true'); ap.add_argument('--verify',action='store_true'); ap.add_argument('--strict',action='store_true')
    ap.add_argument('--buffer',type=int,default=24,help='Python-player decoded frame queue, default 24')
    ap.add_argument('--engine',choices=['auto','native','python'],default='auto')
    ap.add_argument('--ffmpeg'); ap.add_argument('--ffplay'); args=ap.parse_args()

    wav_path=None
    try:
        with open(args.input,'rb') as f:
            h=read_header(f); idx=read_index(f,h); fps=h.fps_num/h.fps_den
            codec=frame_codec_at(f,idx[0][0]) if idx else 0
            print(f'GHV {h.major}.{h.minor} | {h.width}x{h.height} | {fps:.3f} fps | {h.frame_count} frames | q={h.quality}')
            print(f'Duration {h.duration_us/1e6:.3f}s | video=GHVC{codec} | audio={"GHAC1" if h.audio_codec==4 else "none"}')
            if args.info:return
            if not h.frame_count: raise SystemExit('GHV contains no video frames')
            target=max(0,min(h.frame_count-1,int(args.start*fps)))
            start_frame=target
            while start_frame>0 and idx[start_frame][1]!=0:start_frame-=1
            if not args.no_audio and h.audio_codec:
                tmp=tempfile.NamedTemporaryFile(prefix='ghv_',suffix='.wav',delete=False); wav_path=tmp.name; tmp.close(); load_audio_to_wav(f,h,wav_path)
            # Prepare Python state only if we need it. Decode from nearest I frame.
            f.seek(idx[start_frame][0]); prev=None; frame_no=start_frame
            while frame_no<target:
                prev,_,_=decode_one(f,h,prev,args.verify); frame_no+=1
            worker_offset=f.tell(); worker_prev=prev

        native_ok=(codec==4 and find_native_decoder() and find_tool('ffmpeg',args.ffmpeg) and find_tool('ffplay',args.ffplay))
        if args.engine=='native' and not native_ok:
            raise SystemExit('Native playback requested but ghvdecode + FFmpeg + ffplay are not all available.')
        if args.engine in ('auto','native') and native_ok:
            if play_native(args,h,fps,target,wav_path): return
        play_python(args,h,idx,fps,target,start_frame,worker_offset,worker_prev,frame_no,wav_path)
    finally:
        if wav_path:
            try: os.unlink(wav_path)
            except OSError: pass

if __name__=='__main__': main()
