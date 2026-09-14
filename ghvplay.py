#!/usr/bin/env python3
from __future__ import annotations
import argparse, binascii, os, queue, shutil, struct, subprocess, tempfile, threading, time, wave
import numpy as np

from ghv.codec3 import decode_frame as decode_frame3
from ghv.gha import decode_pcm16le as decode_ghac1
from ghv.container import (read_header, read_index, FRAME_FMT, FRAME_SIZE,
                           AUDIO_FMT, AUDIO_SIZE, VFRM, AUD0, unpack_motion)


def find_ffplay(explicit=None):
    if explicit:
        return explicit
    p = shutil.which('ffplay')
    if p:
        return p
    if os.name == 'nt':
        for g in [r'E:\ffmpeg\bin\ffplay.exe', r'C:\ffmpeg\bin\ffplay.exe']:
            if os.path.exists(g):
                return g
    return None


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
        wf.setnchannels(channels)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm)
    return True


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
    if codec != 3:
        raise ValueError(f'unsupported GHV video codec {codec}')
    dx, dy = unpack_motion(meta)
    yuv = decode_frame3(typ, payload, prev, h.width, h.height, raw_size, dx, dy)
    if verify and (binascii.crc32(yuv) & 0xFFFFFFFF) != checksum:
        raise ValueError(f'CRC mismatch at frame {no}')
    return yuv, no, pts


def yuv420_to_bgr_cv(yuv: bytes, w: int, h: int, cv2):
    a = np.frombuffer(yuv, dtype=np.uint8).reshape(h * 3 // 2, w)
    return cv2.cvtColor(a, cv2.COLOR_YUV2BGR_I420)


def main():
    ap = argparse.ArgumentParser(description='GHV 0.4 reference player')
    ap.add_argument('input')
    ap.add_argument('--info', action='store_true')
    ap.add_argument('--start', type=float, default=0.0)
    ap.add_argument('--no-audio', action='store_true')
    ap.add_argument('--verify', action='store_true')
    ap.add_argument('--strict', action='store_true', help='stop instead of recovering at the next keyframe')
    ap.add_argument('--buffer', type=int, default=8, help='decoded-frame queue size, default 8')
    ap.add_argument('--ffplay')
    args = ap.parse_args()

    try:
        import cv2
    except ImportError:
        raise SystemExit('OpenCV required: pip install opencv-python numpy')

    qsize = max(2, min(64, int(args.buffer)))
    with open(args.input, 'rb') as f:
        h = read_header(f)
        idx = read_index(f, h)
        fps = h.fps_num / h.fps_den
        print(f'GHV {h.major}.{h.minor} | {h.width}x{h.height} | {fps:.3f} fps | {h.frame_count} frames | q={h.quality}')
        print(f'Duration {h.duration_us/1e6:.3f}s | video=GHVC3 | audio={"GHAC1" if h.audio_codec == 4 else "none"}')
        if args.info:
            return
        if not h.frame_count:
            raise SystemExit('GHV contains no video frames')

        target = max(0, min(h.frame_count - 1, int(args.start * fps)))
        start_frame = target
        while start_frame > 0 and idx[start_frame][1] != 0:
            start_frame -= 1

        wav_path = None
        audio_proc = None
        if not args.no_audio and h.audio_codec:
            ffplay = find_ffplay(args.ffplay)
            if ffplay:
                tmp = tempfile.NamedTemporaryFile(prefix='ghv_', suffix='.wav', delete=False)
                wav_path = tmp.name
                tmp.close()
                load_audio_to_wav(f, h, wav_path)
            else:
                print('[GHV] ffplay not found; playing video without audio.')

        # Decode from the preceding keyframe to the requested start point once.
        f.seek(idx[start_frame][0])
        prev = None
        frame_no = start_frame
        while frame_no < target:
            prev, _, _ = decode_one(f, h, prev, args.verify)
            frame_no += 1
        worker_offset = f.tell()
        worker_prev = prev

    # Decode ahead on a separate file handle. This smooths decode-time spikes and
    # lets presentation use PTS/wall clock instead of "decode one then sleep".
    dq: queue.Queue = queue.Queue(maxsize=qsize)
    stop = threading.Event()

    def decoder_worker():
        nonlocal worker_offset, worker_prev, frame_no
        try:
            with open(args.input, 'rb') as df:
                df.seek(worker_offset)
                prev_local = worker_prev
                n = frame_no
                while n < h.frame_count and not stop.is_set():
                    try:
                        yuv, no, pts = decode_one(df, h, prev_local, args.verify)
                        if yuv is None:
                            break
                        prev_local = yuv
                        dq.put(('frame', yuv, no, pts), timeout=0.5)
                        n += 1
                    except Exception as exc:
                        if args.strict:
                            dq.put(('error', exc), timeout=0.5)
                            return
                        # Recover at the next indexed I-frame. Broken data between
                        # here and that keyframe is skipped rather than crashing.
                        j = n + 1
                        while j < h.frame_count and idx[j][1] != 0:
                            j += 1
                        if j >= h.frame_count:
                            dq.put(('error', exc), timeout=0.5)
                            return
                        dq.put(('warn', f'decode error at frame {n}: {exc}; recovering at keyframe {j}'), timeout=0.5)
                        df.seek(idx[j][0])
                        prev_local = None
                        n = j
                dq.put(('eof',), timeout=0.5)
        except Exception as exc:
            try:
                dq.put(('error', exc), timeout=0.5)
            except Exception:
                pass

    th = threading.Thread(target=decoder_worker, daemon=True)
    th.start()

    # Wait until a few frames are ready so the display is less likely to stall.
    prebuffer = min(4, qsize)
    deadline = time.perf_counter() + 5.0
    while dq.qsize() < prebuffer and th.is_alive() and time.perf_counter() < deadline:
        time.sleep(0.01)

    if wav_path:
        ffplay = find_ffplay(args.ffplay)
        audio_proc = subprocess.Popen([ffplay, '-nodisp', '-autoexit', '-loglevel', 'quiet',
                                       '-ss', f'{args.start:.6f}', wav_path],
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    win = 'GHV 0.4 Player - Q/ESC to quit'
    cv2.namedWindow(win, cv2.WINDOW_NORMAL)
    cv2.resizeWindow(win, h.width, h.height)
    base = time.perf_counter()
    target_pts_us = int(args.start * 1_000_000)
    dropped = 0
    shown = 0
    try:
        while True:
            item = dq.get()
            tag = item[0]
            if tag == 'eof':
                break
            if tag == 'warn':
                print('[GHV] Warning:', item[1])
                continue
            if tag == 'error':
                raise item[1]
            _, yuv, no, pts = item
            due = max(0.0, (pts - target_pts_us) / 1_000_000.0)
            now = time.perf_counter() - base
            lateness = now - due
            # If decoding/display is badly behind and more frames are buffered,
            # drop presentation only. Decoder state remains intact in the worker.
            if lateness > (1.5 / max(fps, 1e-6)) and not dq.empty():
                dropped += 1
                continue
            if due > now:
                time.sleep(due - now)
            bgr = yuv420_to_bgr_cv(yuv, h.width, h.height, cv2)
            cv2.imshow(win, bgr)
            key = cv2.waitKey(1) & 0xFF
            if key in (27, ord('q'), ord('Q')):
                break
            shown += 1
    finally:
        stop.set()
        th.join(timeout=1.0)
        cv2.destroyAllWindows()
        if audio_proc and audio_proc.poll() is None:
            audio_proc.terminate()
        if wav_path:
            try:
                os.unlink(wav_path)
            except OSError:
                pass
        print(f'[GHV] Playback ended. shown={shown} dropped={dropped}')


if __name__ == '__main__':
    main()
