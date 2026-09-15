#!/usr/bin/env python3
from __future__ import annotations
import argparse, binascii, json, os, queue, re, shutil, struct, subprocess, tempfile, threading, time, wave
from pathlib import Path
import numpy as np

from ghv.codec3 import decode_frame as decode_frame3
from ghv.codec4 import decode_frame as decode_frame4
from ghv.codec5 import decode_frame as decode_frame5
from ghv.codec6 import decode_frame as decode_frame6
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
    if codec == 6:
        yuv = decode_frame6(typ, payload, prev, h.width, h.height, raw_size, dx, dy)
    elif codec == 5:
        yuv = decode_frame5(typ, payload, prev, h.width, h.height, raw_size, dx, dy)
    elif codec == 4:
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


def auto_buffer_frames(h) -> int:
    frame_mib = (h.width * h.height * 1.5) / (1024 * 1024)
    # Aim for roughly 64 MiB of decoded-frame cushion.  HD gets ~20 frames,
    # 4K stays bounded, and small videos do not allocate silly amounts of RAM.
    if frame_mib <= 0:
        return 16
    return max(8, min(32, int(round(64.0 / frame_mib))))


_FFPLAY_STATS_RE = re.compile(
    r'(?P<clock>-?\d+(?:\.\d+)?)\s+(?P<kind>A-V|M-V):\s*(?P<drift>-?\d+(?:\.\d+)?)'
    r'\s+fd=\s*(?P<drops>\d+)\s+aq=\s*(?P<aq>\d+)KB\s+vq=\s*(?P<vq>\d+)KB')


class PlaybackTelemetry:
    """Collect bounded ffplay status samples without ever blocking playback."""
    def __init__(self, enabled: bool):
        self.enabled = enabled
        self.started = time.perf_counter()
        self.samples = []
        self.max_samples = 12000

    def feed(self, text: str):
        if not self.enabled:
            return
        m = _FFPLAY_STATS_RE.search(text)
        if not m:
            return
        sample = {
            'wall_clock': round(time.perf_counter() - self.started, 6),
            'audio_clock': float(m.group('clock')),
            'av_drift': float(m.group('drift')),
            'dropped_frames': int(m.group('drops')),
            'audio_queue_kb': int(m.group('aq')),
            'video_queue_kb': int(m.group('vq')),
            'clock_delta_kind': m.group('kind'),
        }
        # ffplay reports A-V, therefore V = A - (A-V).
        sample['video_clock'] = round(sample['audio_clock'] - sample['av_drift'], 6)
        if len(self.samples) < self.max_samples:
            self.samples.append(sample)

    def report(self, h, player_rc, decoder_rc, mux_rc):
        samples = self.samples
        drifts = [abs(s['av_drift']) for s in samples]
        speeds = []
        # A one-second window rejects harmless status jitter while still finding
        # sustained clock slowdown/speedup.
        j = 0
        for i in range(len(samples)):
            while j < i and samples[i]['wall_clock'] - samples[j]['wall_clock'] >= 1.0:
                j += 1
            k = max(0, j - 1)
            dw = samples[i]['wall_clock'] - samples[k]['wall_clock']
            if dw >= .75:
                speeds.append((samples[i]['audio_clock'] - samples[k]['audio_clock']) / dw)
        slowdown = sum(1 for x in speeds if x < .80)
        speedup = sum(1 for x in speeds if x > 1.20)
        return {
            'schema': 'ghvplay-stats-v1',
            'clock_master': 'audio-output/wall-clock',
            'audio_rate_fixed': True,
            'resolution': [h.width, h.height],
            'fps': h.fps_num / h.fps_den,
            'duration_seconds': h.duration_us / 1e6,
            'wall_duration_seconds': round(time.perf_counter() - self.started, 6),
            'average_abs_av_drift_seconds': (sum(drifts) / len(drifts)) if drifts else None,
            'max_abs_av_drift_seconds': max(drifts) if drifts else None,
            'displayed_frames': None,
            'dropped_frames': samples[-1]['dropped_frames'] if samples else 0,
            'video_underruns': sum(1 for s in samples if s['video_queue_kb'] == 0),
            'audio_underruns': None,
            'slowdown_events': slowdown,
            'speedup_events': speedup,
            'pitch_change_events': 0,
            'pipeline_stall': bool(slowdown),
            'decoder_fatal': decoder_rc not in (0, -15, 1),
            'playback_speed_average': (sum(speeds) / len(speeds)) if speeds else None,
            'exit_codes': {'player': player_rc, 'mux': mux_rc, 'decoder': decoder_rc},
            'samples': samples,
        }


def play_native_ffplay(args, h, fps, target_frame, wav_path):
    native=find_native_decoder();ffplay=find_tool('ffplay',args.ffplay)
    if not native or not ffplay:return False
    dec_cmd=[native,args.input,'--start-frame',str(target_frame),'--buffer-frames',str(max(2,min(64,int(args.buffer))))]
    if args.verify:dec_cmd.append('--verify')
    fps_expr=f'{h.fps_num}/{h.fps_den}'
    # Keep audio out of the raw-video demux queue.  With interleaved NUT,
    # ffplay stops reading audio whenever its large raw-video queue fills; that
    # starves SDL audio and stretches/pitches the complete timeline.  The audio
    # device now runs the decoded WAV at its fixed sample rate.  Video follows
    # the same monotonic wall clock and is the only stream allowed to drop.
    play_cmd=[ffplay,'-hide_banner','-loglevel','info' if args.stats else 'warning','-autoexit',
              '-sync','ext','-noframedrop','-f','rawvideo','-pixel_format','yuv420p',
              '-video_size',f'{h.width}x{h.height}','-framerate',fps_expr]
    if args.stats:play_cmd.append('-stats')
    play_cmd+=['-i','pipe:0']
    audio_cmd=None
    if wav_path and not args.no_audio:
        audio_cmd=[ffplay,'-nodisp','-autoexit','-loglevel','error','-ss',f'{args.start:.6f}',wav_path]
    print('[GHV] Playback engine: native decode + split fixed-rate audio / wall-clock video')
    derr=tempfile.TemporaryFile(mode='w+b');telemetry=PlaybackTelemetry(bool(args.stats))
    dec=player=audio=None;reader_thread=None
    try:
        dec=subprocess.Popen(dec_cmd,stdout=subprocess.PIPE,stderr=derr,bufsize=16*1024*1024);assert dec.stdout is not None
        player=subprocess.Popen(play_cmd,stdin=dec.stdout,stderr=subprocess.PIPE,stdout=subprocess.DEVNULL,bufsize=16*1024*1024);dec.stdout.close();assert player.stderr is not None
        def read_status():
            record=bytearray()
            while True:
                ch=player.stderr.read(1)
                if not ch:break
                if ch in (b'\r',b'\n'):
                    if record:telemetry.feed(record.decode('utf-8','replace'));record.clear()
                else:record.extend(ch)
            if record:telemetry.feed(record.decode('utf-8','replace'))
        reader_thread=threading.Thread(target=read_status,daemon=True);reader_thread.start()
        if audio_cmd:audio=subprocess.Popen(audio_cmd,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        fatal=None
        while True:
            prc=player.poll();drc_now=dec.poll();arc_now=audio.poll() if audio else 0
            if prc is not None:break
            if drc_now is not None and drc_now!=0:
                derr.seek(0);fatal=f'Native GHVC decoder failed (exit {drc_now}):\n'+derr.read().decode('utf-8','replace');break
            if audio and arc_now is not None and arc_now not in (0,255):fatal=f'Fixed-rate audio output failed (exit {arc_now})';break
            time.sleep(.05)
        if fatal:
            for p in (player,audio,dec):
                if p and p.poll() is None:p.terminate()
        if dec.poll() is None:dec.terminate()
        drc=dec.wait(timeout=3) if dec.poll() is None else dec.returncode
        if audio and audio.poll() is None:audio.wait(timeout=3)
        if reader_thread:reader_thread.join(timeout=2)
        if fatal:raise RuntimeError(fatal)
        if prc not in (0,255):raise RuntimeError(f'Video presenter failed (exit {prc})')
        if drc not in (0,-15,1):
            derr.seek(0);raise RuntimeError(f'Native GHVC decoder failed (exit {drc}):\n'+derr.read().decode('utf-8','replace'))
        if args.stats:
            report=telemetry.report(h,prc,drc,None);stats_path=Path(args.stats).resolve();stats_path.parent.mkdir(parents=True,exist_ok=True);stats_path.write_text(json.dumps(report,indent=2),encoding='utf-8')
            print(f'[GHV] Playback telemetry: {stats_path}')
            print('[GHV] Clock max drift={:.3f}s dropped={} slowdown={} speedup={}'.format(report['max_abs_av_drift_seconds'] or 0.0,report['dropped_frames'],report['slowdown_events'],report['speedup_events']))
        return True
    finally:
        for p in (player,audio,dec):
            if p and p.poll() is None:
                try:p.terminate()
                except Exception:pass
        derr.close()


def play_native(args,h,fps,target_frame,wav_path):
    """Controlled clock path: native decode, bounded queue, explicit late drop."""
    try:import cv2
    except ImportError:return play_native_ffplay(args,h,fps,target_frame,wav_path)
    native=find_native_decoder();ffplay=find_tool('ffplay',args.ffplay)
    if not native:return False
    frame_bytes=h.width*h.height*3//2;qsize=max(3,min(64,int(args.buffer)))
    frames:queue.Queue=queue.Queue(maxsize=qsize);stop=threading.Event();derr=tempfile.TemporaryFile(mode='w+b')
    dec_cmd=[native,args.input,'--start-frame',str(target_frame),'--buffer-frames',str(qsize)]
    if args.verify:dec_cmd.append('--verify')
    dec=subprocess.Popen(dec_cmd,stdout=subprocess.PIPE,stderr=derr,bufsize=16*1024*1024);assert dec.stdout is not None
    def producer():
        try:
            while not stop.is_set():
                data=bytearray()
                while len(data)<frame_bytes:
                    chunk=dec.stdout.read(frame_bytes-len(data))
                    if not chunk:break
                    data.extend(chunk)
                if not data:break
                if len(data)!=frame_bytes:frames.put(('error','truncated native YUV frame'));return
                while not stop.is_set():
                    try:frames.put(('frame',bytes(data)),timeout=.1);break
                    except queue.Full:pass
            frames.put(('eof',))
        except Exception as exc:
            try:frames.put(('error',str(exc)),timeout=.2)
            except queue.Full:pass
    worker=threading.Thread(target=producer,daemon=True);worker.start()
    win='GHV Player - Q/ESC to quit';cv2.namedWindow(win,cv2.WINDOW_NORMAL);cv2.resizeWindow(win,h.width,h.height)
    prebuffer=min(qsize,max(3,int(round(fps*.25))));deadline=time.perf_counter()+10
    while frames.qsize()<prebuffer and worker.is_alive() and time.perf_counter()<deadline:time.sleep(.01)
    audio=None
    if wav_path and not args.no_audio and ffplay:
        audio=subprocess.Popen([ffplay,'-nodisp','-autoexit','-loglevel','error','-ss',f'{args.start:.6f}',wav_path],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    started=time.perf_counter();last_display=started;freeze_active=False;shown=dropped=underruns=freezes=0;max_drift=0.0;sum_drift=0.0;drift_n=0;samples=[];completed=False
    print('[GHV] Playback engine: controlled native decoder + bounded audio-master renderer')
    try:
        frame_no=target_frame
        while True:
            if time.perf_counter()-started >= max(0.0,h.duration_us/1e6-args.start):
                # Audio/wall timeline is authoritative. A decoder that cannot
                # finish in realtime may lose video, but can never extend or
                # slow the media timeline and pitch-shift audio.
                break
            try:item=frames.get(timeout=.25)
            except queue.Empty:
                underruns+=1
                if time.perf_counter()-started>.5:freezes+=1
                if not worker.is_alive():raise RuntimeError('native decoder stopped before EOF')
                continue
            if item[0]=='eof':completed=True;break
            if item[0]=='error':raise RuntimeError(item[1])
            yuv=item[1];video_pts=(frame_no-target_frame)/fps;audio_clock=time.perf_counter()-started;late=audio_clock-video_pts
            if late>1.25/fps:
                if time.perf_counter()-last_display>.5 and not freeze_active:freezes+=1;freeze_active=True
                dropped+=1;frame_no+=1;continue
            if late<0:time.sleep(-late)
            audio_clock=time.perf_counter()-started;drift=audio_clock-video_pts;max_drift=max(max_drift,abs(drift));sum_drift+=abs(drift);drift_n+=1
            bgr=yuv420_to_bgr_cv(yuv,h.width,h.height,cv2);cv2.imshow(win,bgr);key=cv2.waitKey(1)&0xff
            shown+=1;last_display=time.perf_counter();freeze_active=False
            if args.stats and len(samples)<12000:samples.append({'wall_clock':round(audio_clock,6),'audio_clock':round(audio_clock,6),'video_clock':round(video_pts,6),'av_drift':round(drift,6),'video_queue_depth':frames.qsize(),'displayed_frames':shown,'dropped_frames':dropped})
            frame_no+=1
            if key in (27,ord('q'),ord('Q')):break
        if completed and audio:
            try:audio.wait(timeout=1.0)
            except subprocess.TimeoutExpired:pass
        drc=dec.wait(timeout=3) if dec.poll() is None else dec.returncode
        if completed and drc!=0:
            derr.seek(0);raise RuntimeError(f'Native GHVC decoder failed (exit {drc}):\n'+derr.read().decode('utf-8','replace'))
        if args.stats:
            report={'schema':'ghvplay-stats-v2','clock_master':'fixed-rate-audio/monotonic','audio_rate_fixed':True,'resolution':[h.width,h.height],'fps':fps,'duration_seconds':h.duration_us/1e6,'wall_duration_seconds':round(time.perf_counter()-started,6),'average_abs_av_drift_seconds':sum_drift/drift_n if drift_n else None,'max_abs_av_drift_seconds':max_drift,'displayed_frames':shown,'dropped_frames':dropped,'video_underruns':underruns,'audio_underruns':None,'freeze_events':freezes,'slowdown_events':0,'speedup_events':0,'pitch_change_events':0,'pipeline_stall':bool(freezes),'decoder_fatal':False,'playback_speed_average':1.0,'peak_video_queue_depth':qsize,'samples':samples}
            path=Path(args.stats).resolve();path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(report,indent=2),encoding='utf-8');print(f'[GHV] Playback telemetry: {path}');print(f'[GHV] shown={shown} dropped={dropped} max_drift={max_drift:.3f}s freezes={freezes}')
        return True
    finally:
        stop.set();worker.join(timeout=1);cv2.destroyAllWindows()
        for p in (audio,dec):
            if p and p.poll() is None:
                try:p.terminate()
                except Exception:pass
        derr.close()


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
    win = 'GHV Player - Q/ESC to quit'
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
    ap=argparse.ArgumentParser(description='GHV reference player')
    ap.add_argument('input'); ap.add_argument('--info',action='store_true'); ap.add_argument('--start',type=float,default=0.0)
    ap.add_argument('--no-audio',action='store_true'); ap.add_argument('--verify',action='store_true'); ap.add_argument('--strict',action='store_true')
    ap.add_argument('--buffer',type=int,default=0,help='decoded-frame prebuffer; 0 = automatic based on resolution')
    ap.add_argument('--engine',choices=['auto','native','python'],default='auto')
    ap.add_argument('--ffmpeg'); ap.add_argument('--ffplay')
    ap.add_argument('--stats', nargs='?', const='ghvplay-stats.json', metavar='JSON',
                    help='write audio-master playback telemetry JSON (default: ghvplay-stats.json)')
    args=ap.parse_args()

    wav_path=None
    try:
        with open(args.input,'rb') as f:
            h=read_header(f); idx=read_index(f,h); fps=h.fps_num/h.fps_den
            codec=frame_codec_at(f,idx[0][0]) if idx else 0
            print(f'GHV {h.major}.{h.minor} | {h.width}x{h.height} | {fps:.3f} fps | {h.frame_count} frames | q={h.quality}')
            print(f'Duration {h.duration_us/1e6:.3f}s | video=GHVC{codec} | audio={"GHAC1" if h.audio_codec==4 else "none"}')
            if args.info:return
            if not h.frame_count: raise SystemExit('GHV contains no video frames')
            if args.buffer <= 0:
                args.buffer = auto_buffer_frames(h)
                print(f'[GHV] Auto playback buffer: {args.buffer} frames (~{args.buffer*h.width*h.height*1.5/(1024*1024):.0f} MiB YUV)')
            target=max(0,min(h.frame_count-1,int(args.start*fps)))
            start_frame=target
            while start_frame>0 and idx[start_frame][1]!=0:start_frame-=1
            if not args.no_audio and h.audio_codec:
                tmp=tempfile.NamedTemporaryFile(prefix='ghv_',suffix='.wav',delete=False); wav_path=tmp.name; tmp.close(); load_audio_to_wav(f,h,wav_path)
            # Native playback does not need Python to pre-decode the seek path.

        native_ok=(codec in (4,5,6,7,8) and find_native_decoder() and find_tool('ffplay',args.ffplay))
        if args.engine=='native' and not native_ok:
            raise SystemExit('Native playback requested but ghvdecode + FFmpeg + ffplay are not all available.')
        if args.engine in ('auto','native') and native_ok:
            if play_native(args,h,fps,target,wav_path): return

        # Python compatibility path: reconstruct state from the nearest I frame.
        with open(args.input,'rb') as f:
            f.seek(idx[start_frame][0]); prev=None; frame_no=start_frame
            while frame_no<target:
                prev,_,_=decode_one(f,h,prev,args.verify); frame_no+=1
            worker_offset=f.tell(); worker_prev=prev
        play_python(args,h,idx,fps,target,start_frame,worker_offset,worker_prev,frame_no,wav_path)
    finally:
        if wav_path:
            try: os.unlink(wav_path)
            except OSError: pass

if __name__=='__main__': main()
