#!/usr/bin/env python3
from __future__ import annotations
import argparse, binascii, json, os, shutil, struct, subprocess, tempfile, time
from fractions import Fraction
from pathlib import Path

from ghv.codec4 import quantize_yuv420, encode_frame as encode_frame4
from ghv.gha import encode_pcm16le as encode_ghac1
from ghv.paths import native_binary
from ghv.version import version_summary
from ghv.container import (Header, HEADER_SIZE, FRAME_FMT, AUDIO_FMT,
                           INDEX_HEAD_FMT, INDEX_ENTRY_FMT, VFRM, AUD0, INDX,
                           pack_motion, read_header, read_index)

PRESETS = {
    # GHVC6 includes local 32x32 block motion, but its search is still experimental.
    # Speed-oriented presets keep it off; Quality enables a small search range.
    'veryfast': dict(quality=68, keyint=90,  motion=0, scene=32.0),
    'fast':     dict(quality=74, keyint=105, motion=0, scene=31.0),
    'compact':  dict(quality=72, keyint=150, motion=0, scene=29.0),
    'balanced': dict(quality=78, keyint=120, motion=0, scene=30.0),
    'quality':  dict(quality=88, keyint=150, motion=2, scene=29.0),
}


def find_tool(name: str, explicit: str | None = None) -> str:
    if explicit:
        return explicit
    p = shutil.which(name)
    if p:
        return p
    if os.name == 'nt':
        for g in [rf'E:\\ffmpeg\\bin\\{name}.exe', rf'C:\\ffmpeg\\bin\\{name}.exe']:
            if os.path.exists(g):
                return g
    raise SystemExit(f'{name} not found. Put FFmpeg in PATH or pass --{name}.')


def ffprobe_info(path: str, ffprobe: str):
    cmd = [ffprobe, '-v', 'error', '-print_format', 'json', '-show_streams', '-show_format', path]
    data = json.loads(subprocess.check_output(cmd, text=True, encoding='utf-8', errors='replace'))
    videos = [s for s in data.get('streams', []) if s.get('codec_type') == 'video']
    audios = [s for s in data.get('streams', []) if s.get('codec_type') == 'audio']
    if not videos:
        raise SystemExit('No video stream found')
    return videos[0], (audios[0] if audios else None), data.get('format', {})


def parse_rate(s: str) -> Fraction:
    try:
        f = Fraction(s)
        if f <= 0:
            raise ValueError
        return f.limit_denominator(100000)
    except Exception:
        return Fraction(30, 1)


def duration_seconds(vs, fmt) -> float:
    for obj, key in ((vs, 'duration'), (fmt, 'duration')):
        try:
            d = float(obj.get(key, 0) or 0)
            if d > 0:
                return d
        except Exception:
            pass
    return 0.0


def find_native_core() -> str | None:
    p = native_binary('ghvcore')
    if p.is_file():
        return str(p)
    return None


def encode_video_native(video_cmd, native_core: str, w: int, h: int, quality: int,
                        keyint: int, scene_threshold: float, motion_range: int,
                        est_frames: int, f, fps_num: int, fps_den: int, threads: int = 0):
    """Stream GHVC6 records straight into the final GHV container.

    v0.3 wrote the complete native intermediate to a temporary file and then
    copied it into the container. GHV 0.4 removes that double I/O and the huge
    temporary-disk requirement: ghvcore writes an uncounted GHS6 stream to
    stdout while this wrapper immediately writes each record into .ghv.
    """
    frame_offsets: list[tuple[int, int]] = []
    ff = subprocess.Popen(video_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=16 * 1024 * 1024)
    assert ff.stdout is not None
    err_tmp = tempfile.TemporaryFile(mode='w+b')
    cmd = [native_core, str(w), str(h), str(quality), str(keyint), str(scene_threshold),
           str(motion_range), '-', str(est_frames)]
    env = os.environ.copy()
    if threads and threads > 0:
        env['OMP_NUM_THREADS'] = str(threads)
    core = subprocess.Popen(cmd, stdin=ff.stdout, stdout=subprocess.PIPE, stderr=err_tmp, bufsize=16 * 1024 * 1024, env=env)
    ff.stdout.close()
    assert core.stdout is not None
    count = 0
    t0 = time.perf_counter()
    try:
        head = core.stdout.read(20)
        if len(head) != 20 or head[:4] != b'GHS6':
            raise RuntimeError('bad GHVC6 native stream header')
        ver, sw, sh, raw_frame_size = struct.unpack_from('<IIII', head, 4)
        if ver != 6 or sw != w or sh != h or raw_frame_size != w * h * 3 // 2:
            raise RuntimeError('GHVC6 native stream metadata mismatch')

        while True:
            rh = core.stdout.read(16)
            if not rh:
                break
            if len(rh) != 16:
                raise RuntimeError('truncated GHVC6 native frame record')
            typ = rh[0]
            dx = struct.unpack_from('<b', rh, 1)[0]
            dy = struct.unpack_from('<b', rh, 2)[0]
            raw_size, packed_size, checksum = struct.unpack_from('<III', rh, 4)
            payload = core.stdout.read(packed_size)
            if len(payload) != packed_size:
                raise RuntimeError('truncated GHVC6 native frame payload')
            off = f.tell()
            pts_us = (count * fps_den * 1_000_000) // fps_num
            f.write(struct.pack(FRAME_FMT, VFRM, count, pts_us, typ, 6, pack_motion(dx, dy),
                                raw_size, packed_size, checksum))
            f.write(payload)
            frame_offsets.append((off, typ))
            count += 1
            if count % 15 == 0:
                elapsed = max(.001, time.perf_counter() - t0)
                encfps = count / elapsed
                pct = (100.0 * count / est_frames) if est_frames else 0.0
                eta = ((est_frames - count) / encfps) if est_frames and encfps > 0 else -1
                print(f'PROGRESS frame={count} total={est_frames} pct={pct:.2f} fps={encfps:.2f} '
                      f'eta={eta:.1f} size_mib={f.tell()/(1024*1024):.2f}', flush=True)

        rc_core = core.wait()
        err = ff.stderr.read().decode('utf-8', 'replace') if ff.stderr else ''
        rc_ff = ff.wait()
        err_tmp.seek(0)
        core_err = err_tmp.read().decode('utf-8', 'replace')
        if core_err.strip():
            for line in core_err.splitlines():
                print('[GHV native6] ' + line, flush=True)
        if rc_core != 0:
            raise RuntimeError(f'Native GHVC6 core failed with exit {rc_core}:\n{core_err}')
        if rc_ff != 0:
            raise RuntimeError('FFmpeg video decode failed:\n' + err)
        return count, frame_offsets
    finally:
        try:
            core.stdout.close()
        except Exception:
            pass
        err_tmp.close()



def encode_video_native_direct(video_cmd, native_core: str, out_path: Path, w: int, h: int,
                               quality: int, keyint: int, scene_threshold: float,
                               motion_range: int, est_frames: int, fps_num: int,
                               fps_den: int, threads: int = 0, codec: int = 7,
                               profile: bool = False, rd_finalists: int = 2) -> int:
    """Native core writes VFRM + INDX + header itself.

    Python never receives packed video frames, which removes one full copy and
    thousands of per-frame struct/write calls on long 1080p encodes.
    """
    ff_err = tempfile.TemporaryFile(mode='w+b')
    ff = subprocess.Popen(video_cmd, stdout=subprocess.PIPE, stderr=ff_err,
                          bufsize=16 * 1024 * 1024)
    assert ff.stdout is not None
    cmd = [native_core, str(w), str(h), str(quality), str(keyint),
           str(scene_threshold), str(motion_range), str(out_path),
           str(est_frames), '--ghv', str(fps_num), str(fps_den), '--codec', str(codec)]
    if codec == 9:
        cmd += ['--rd-finalists', str(rd_finalists)]
    if profile:
        cmd.append('--profile')
    env = os.environ.copy()
    if threads and threads > 0:
        env['OMP_NUM_THREADS'] = str(threads)
    core = subprocess.Popen(cmd, stdin=ff.stdout, stdout=subprocess.DEVNULL,
                            stderr=subprocess.PIPE, text=True,
                            encoding='utf-8', errors='replace', bufsize=1, env=env)
    ff.stdout.close()
    assert core.stderr is not None
    try:
        for raw_line in core.stderr:
            line = raw_line.rstrip('\r\n')
            if line.startswith('GHV_PROGRESS '):
                print('PROGRESS ' + line[len('GHV_PROGRESS '):], flush=True)
            elif line:
                print(f'[GHV native{codec}] ' + line, flush=True)
        rc_core = core.wait()
        rc_ff = ff.wait()
        ff_err.seek(0)
        ferr = ff_err.read().decode('utf-8', 'replace')
        if rc_core != 0:
            raise RuntimeError(f'Native GHVC{codec} direct encoder failed with exit {rc_core}')
        if rc_ff != 0:
            raise RuntimeError('FFmpeg video decode failed:\n' + ferr)
        with open(out_path, 'rb') as vf:
            vh = read_header(vf)
            idx = read_index(vf, vh)
        if vh.minor != codec or vh.frame_count != len(idx):
            raise RuntimeError('native direct GHV header/index validation failed')
        return vh.frame_count
    finally:
        ff_err.close()
        try:
            if core.poll() is None:
                core.terminate()
        except Exception:
            pass
        try:
            if ff.poll() is None:
                ff.terminate()
        except Exception:
            pass

def main():
    ap = argparse.ArgumentParser(description='Encode FFmpeg-readable video to GHV / native GHVC6 through GHVC9')
    ap.add_argument('--version', action='version', version=version_summary())
    ap.add_argument('input')
    ap.add_argument('output')
    ap.add_argument('--preset', choices=sorted(PRESETS), default='balanced')
    ap.add_argument('-q', '--quality', type=int, default=None, help='override preset quality 1..100')
    ap.add_argument('--keyint', type=int, default=None, help='override maximum keyframe interval')
    ap.add_argument('--motion-range', type=int, default=None, help='0..31 pixels global motion search')
    ap.add_argument('--scene-threshold', type=float, default=None)
    ap.add_argument('--audio-quality', choices=['hq', 'compact'], default='hq')
    ap.add_argument('--native', choices=['auto', 'on', 'off'], default='auto')
    ap.add_argument('--threads', type=int, default=0, help='native encoder threads; 0 = automatic')
    ap.add_argument('--profile', action='store_true', help='print native per-stage timing')
    ap.add_argument('--codec', type=int, choices=[6, 7, 8, 9], default=9, help='video codec version (default: GHVC9)')
    ap.add_argument('--no-audio', action='store_true')
    ap.add_argument('--audio-rate', type=int, default=0, help='0 = preserve source sample rate')
    ap.add_argument('--ffmpeg')
    ap.add_argument('--ffprobe')
    args = ap.parse_args()

    pp = PRESETS[args.preset]
    quality = max(1, min(100, args.quality if args.quality is not None else pp['quality']))
    keyint = max(1, args.keyint if args.keyint is not None else pp['keyint'])
    motion_range = max(0, min(31, args.motion_range if args.motion_range is not None else pp['motion']))
    scene_threshold = float(args.scene_threshold if args.scene_threshold is not None else pp['scene'])
    if args.codec == 7 and motion_range:
        print('[GHV] GHVC7 profile 0 has no motion-vector syntax yet; ignoring motion range.', flush=True)
        motion_range = 0
    if args.codec >= 8 and args.motion_range is None:
        motion_range = 4
    # GHVC9 presets change search effort, never decoder syntax or quantization
    # behind the user's back. Balanced keeps zero plus the best sampled-SAD MV;
    # Quality spends more RD work and Compact sits between them.
    rd_finalists = {'veryfast': 0, 'fast': 1, 'balanced': 1, 'compact': 2, 'quality': 3}[args.preset]

    ffmpeg = find_tool('ffmpeg', args.ffmpeg)
    ffprobe = find_tool('ffprobe', args.ffprobe)
    vs, aus, fmt = ffprobe_info(args.input, ffprobe)
    src_w, src_h = int(vs['width']), int(vs['height'])
    w, h = src_w & ~1, src_h & ~1
    if (w, h) != (src_w, src_h):
        print(f'[GHV] Odd input size {src_w}x{src_h}; cropping to {w}x{h}', flush=True)
    fps = parse_rate(vs.get('avg_frame_rate') or vs.get('r_frame_rate') or '30/1')
    fps_num, fps_den = fps.numerator, fps.denominator
    est_duration = duration_seconds(vs, fmt)
    est_frames = int(round(est_duration * float(fps))) if est_duration > 0 else 0
    frame_bytes = w * h * 3 // 2

    out_path = Path(args.output)
    if out_path.suffix.lower() != '.ghv':
        print(f'[GHV] Note: standard extension is .ghv (requested {out_path.suffix or "no extension"})', flush=True)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    header = Header(flags=0, width=w, height=h, fps_num=fps_num, fps_den=fps_den,
                    frame_count=0, keyint=keyint, quality=quality,
                    audio_rate=0, audio_channels=0, audio_codec=0, audio_samples=0,
                    frames_offset=HEADER_SIZE, audio_offset=0, index_offset=0,
                    duration_us=0, major=0, minor=args.codec)

    vf = f'crop={w}:{h}:0:0'
    video_cmd = [ffmpeg, '-v', 'error', '-i', args.input, '-map', '0:v:0', '-an',
                 '-vf', vf, '-f', 'rawvideo', '-pix_fmt', 'yuv420p', '-']

    native_core = find_native_core() if args.native != 'off' else None
    if args.native == 'on' and not native_core:
        raise SystemExit(f'Native GHVC{args.codec} core requested but not built. Run native\\build_windows.bat.')
    use_native = bool(native_core)
    if args.codec >= 7 and not use_native:
        raise SystemExit(f'GHVC{args.codec} requires the native C++ core. Build native/ghvcore first.')
    engine = 'Native C++' if use_native else 'Fast NumPy'
    print(f'[GHV] {w}x{h} @ {float(fps):.3f} fps | GHVC{args.codec} | {args.preset} | engine={engine}', flush=True)
    print(f'[GHV] quality={quality} keyint={keyint} motion=±{motion_range} scene={scene_threshold:g} threads={args.threads or "auto"}', flush=True)
    if est_frames:
        print(f'[GHV] Estimated frames: {est_frames}', flush=True)
    if not use_native:
        print('[GHV] Native core not found; using NumPy reference encoder. Native build is recommended for long videos.', flush=True)

    frame_offsets: list[tuple[int, int]] = []
    frame_count = 0
    flags = 0
    audio_offset = audio_samples = audio_channels = audio_codec = audio_rate = 0
    duration_us = 0
    t0 = time.perf_counter()

    try:
        if use_native:
            print('[GHV] Direct native mux enabled: Python will not copy packed video frames.', flush=True)
            frame_count = encode_video_native_direct(
                video_cmd, native_core, out_path, w, h, quality, keyint,
                scene_threshold, motion_range, est_frames, fps_num, fps_den,
                max(0, args.threads), args.codec, args.profile, rd_finalists)
            with open(out_path, 'r+b') as f:
                base_header = read_header(f)
                # The native encoder has already written the complete video
                # stream and index.  Audio is appended *after* the index; all
                # sections are located by offsets, so no video bytes move.
                f.seek(0, os.SEEK_END)
                if aus is not None and not args.no_audio:
                    audio_rate = int(args.audio_rate) if int(args.audio_rate) > 0 else int(aus.get('sample_rate') or 48000)
                    audio_channels = max(1, min(2, int(aus.get('channels') or 2)))
                    audio_cmd = [ffmpeg, '-v', 'error', '-i', args.input, '-map', '0:a:0', '-vn',
                                 '-f', 's16le', '-acodec', 'pcm_s16le', '-ac', str(audio_channels),
                                 '-ar', str(audio_rate), '-']
                    print('[GHV] Encoding embedded audio with GHAC1...', flush=True)
                    pcm = subprocess.check_output(audio_cmd)
                    audio_samples = len(pcm) // (2 * audio_channels)
                    bits = 8 if args.audio_quality == 'hq' else 6
                    gha = encode_ghac1(pcm, audio_rate, audio_channels, bits=bits,
                                       block_frames=32, quality=(92 if bits == 8 else 74))
                    audio_codec = 4
                    audio_offset = f.tell()
                    f.write(struct.pack(AUDIO_FMT, AUD0, audio_codec, len(gha), audio_samples))
                    f.write(gha)
                    flags |= 1
                    print(f'[GHV] Audio PCM {len(pcm)/(1024*1024):.2f} MiB -> GHAC1 {len(gha)/(1024*1024):.2f} MiB', flush=True)

                video_dur_us = (frame_count * fps_den * 1_000_000) // fps_num if frame_count else 0
                audio_dur_us = (audio_samples * 1_000_000) // audio_rate if audio_rate and audio_samples else 0
                duration_us = max(video_dur_us, audio_dur_us)
                header = Header(flags=flags, width=w, height=h, fps_num=fps_num, fps_den=fps_den,
                                frame_count=frame_count, keyint=keyint, quality=quality,
                                audio_rate=audio_rate, audio_channels=audio_channels, audio_codec=audio_codec,
                                audio_samples=audio_samples, frames_offset=HEADER_SIZE, audio_offset=audio_offset,
                                index_offset=base_header.index_offset, duration_us=duration_us, major=0, minor=args.codec)
                f.seek(0); f.write(header.pack()); f.flush()
                final_size = os.fstat(f.fileno()).st_size
        else:
            with open(out_path, 'wb+') as f:
                f.write(header.pack())
                prev = None
                proc = subprocess.Popen(video_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=16 * 1024 * 1024)
                assert proc.stdout is not None
                repeats = 0
                while True:
                    raw = proc.stdout.read(frame_bytes)
                    if not raw:
                        break
                    if len(raw) != frame_bytes:
                        raise RuntimeError(f'truncated raw frame: {len(raw)} / {frame_bytes}')
                    yuv = quantize_yuv420(raw, w, h, quality)
                    force_i = (frame_count % keyint == 0)
                    typ, packed, raw_size, dx, dy, recon = encode_frame4(
                        yuv, prev, w, h, quality, force_i, scene_threshold, motion_range)
                    if typ == 2:
                        repeats += 1
                    checksum = binascii.crc32(recon) & 0xFFFFFFFF
                    off = f.tell()
                    pts_us = (frame_count * fps_den * 1_000_000) // fps_num
                    f.write(struct.pack(FRAME_FMT, VFRM, frame_count, pts_us, typ, 4,
                                        pack_motion(dx, dy), raw_size, len(packed), checksum))
                    f.write(packed)
                    frame_offsets.append((off, typ))
                    prev = recon
                    frame_count += 1
                    if frame_count % 15 == 0:
                        elapsed = max(.001, time.perf_counter() - t0)
                        encfps = frame_count / elapsed
                        mb = f.tell() / (1024 * 1024)
                        pct = (100.0 * frame_count / est_frames) if est_frames else 0.0
                        eta = ((est_frames - frame_count) / encfps) if est_frames and encfps > 0 else -1
                        print(f'PROGRESS frame={frame_count} total={est_frames} pct={pct:.2f} '
                              f'fps={encfps:.2f} eta={eta:.1f} repeats={repeats} size_mib={mb:.2f}', flush=True)
                proc.stdout.close()
                err = proc.stderr.read().decode('utf-8', 'replace') if proc.stderr else ''
                rc = proc.wait()
                if rc != 0:
                    raise RuntimeError('FFmpeg video decode failed:\n' + err)

                if aus is not None and not args.no_audio:
                    audio_rate = int(args.audio_rate) if int(args.audio_rate) > 0 else int(aus.get('sample_rate') or 48000)
                    audio_channels = max(1, min(2, int(aus.get('channels') or 2)))
                    audio_cmd = [ffmpeg, '-v', 'error', '-i', args.input, '-map', '0:a:0', '-vn',
                                 '-f', 's16le', '-acodec', 'pcm_s16le', '-ac', str(audio_channels),
                                 '-ar', str(audio_rate), '-']
                    print('[GHV] Encoding embedded audio with GHAC1...', flush=True)
                    pcm = subprocess.check_output(audio_cmd)
                    audio_samples = len(pcm) // (2 * audio_channels)
                    bits = 8 if args.audio_quality == 'hq' else 6
                    gha = encode_ghac1(pcm, audio_rate, audio_channels, bits=bits,
                                       block_frames=32, quality=(92 if bits == 8 else 74))
                    audio_codec = 4
                    audio_offset = f.tell()
                    f.write(struct.pack(AUDIO_FMT, AUD0, audio_codec, len(gha), audio_samples))
                    f.write(gha)
                    flags |= 1
                    print(f'[GHV] Audio PCM {len(pcm)/(1024*1024):.2f} MiB -> GHAC1 {len(gha)/(1024*1024):.2f} MiB', flush=True)

                index_offset = f.tell()
                f.write(struct.pack(INDEX_HEAD_FMT, INDX, len(frame_offsets)))
                for off, typ in frame_offsets:
                    f.write(struct.pack(INDEX_ENTRY_FMT, off, typ))

                video_dur_us = (frame_count * fps_den * 1_000_000) // fps_num if frame_count else 0
                audio_dur_us = (audio_samples * 1_000_000) // audio_rate if audio_rate and audio_samples else 0
                duration_us = max(video_dur_us, audio_dur_us)
                header = Header(flags=flags, width=w, height=h, fps_num=fps_num, fps_den=fps_den,
                                frame_count=frame_count, keyint=keyint, quality=quality,
                                audio_rate=audio_rate, audio_channels=audio_channels, audio_codec=audio_codec,
                                audio_samples=audio_samples, frames_offset=HEADER_SIZE, audio_offset=audio_offset,
                                index_offset=index_offset, duration_us=duration_us, major=0, minor=6)
                f.seek(0); f.write(header.pack()); f.flush()
                final_size = os.fstat(f.fileno()).st_size
    except Exception:
        # A half-written media file is more confusing than no output at all.
        try:
            if out_path.exists():
                out_path.unlink()
        except OSError:
            pass
        raise

    elapsed = max(.001, time.perf_counter() - t0)
    print(f'[GHV] Done: {out_path}', flush=True)
    print(f'[GHV] RESULT frames={frame_count} duration={duration_us/1e6:.3f} '
          f'size_mib={final_size/(1024*1024):.3f} elapsed={elapsed:.3f} avg_fps={frame_count/elapsed:.3f}', flush=True)


if __name__ == '__main__':
    main()
