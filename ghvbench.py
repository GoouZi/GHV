#!/usr/bin/env python3
from __future__ import annotations
import argparse, json, os, re, shutil, struct, subprocess, sys, tempfile, time
from pathlib import Path
from ghv.container import read_header, read_index, FRAME_FMT, FRAME_SIZE

ROOT = Path(__file__).resolve().parent
RESULT_RE = re.compile(r'RESULT\s+frames=(\d+)\s+duration=([0-9.]+)\s+size_mib=([0-9.]+)\s+elapsed=([0-9.]+)\s+avg_fps=([0-9.]+)')


def probe_source(path: Path):
    ffprobe=shutil.which('ffprobe')
    if not ffprobe:return {}
    cmd=[ffprobe,'-v','error','-select_streams','v:0','-show_entries',
         'stream=width,height,avg_frame_rate,bit_rate:format=duration,size,bit_rate','-of','json',str(path)]
    try:
        data=json.loads(subprocess.check_output(cmd,text=True,encoding='utf-8',errors='replace'))
        v=(data.get('streams') or [{}])[0];fmt=data.get('format') or {}
        return {'width':int(v.get('width') or 0),'height':int(v.get('height') or 0),
                'fps':v.get('avg_frame_rate'),'duration_seconds':float(fmt.get('duration') or 0),
                'video_bitrate_bps':int(v.get('bit_rate') or 0),'container_bitrate_bps':int(fmt.get('bit_rate') or 0)}
    except Exception:return {}


def inspect_output(path: Path):
    with open(path,'rb') as f:
        h=read_header(f);idx=read_index(f,h);codec=0
        if idx:
            f.seek(idx[0][0]);rh=f.read(FRAME_SIZE)
            if len(rh)==FRAME_SIZE:codec=struct.unpack(FRAME_FMT,rh)[4]
    return h,codec


def measure_decode(path: Path,frames: int):
    dec=ROOT/'native'/'bin'/('ghvdecode.exe' if os.name=='nt' else 'ghvdecode')
    if not dec.is_file():return None
    h,_=inspect_output(path);n=h.frame_count if frames==0 else max(1,min(h.frame_count,frames))
    p=subprocess.run([str(dec),str(path),'--no-output','--verify','--frames',str(n)],
                     stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace')
    if p.returncode:return None
    m=re.search(r'fps=([0-9.]+)',p.stderr);return float(m.group(1)) if m else None


def measure_quality(source: Path,path: Path):
    ffmpeg=shutil.which('ffmpeg');dec=ROOT/'native'/'bin'/('ghvdecode.exe' if os.name=='nt' else 'ghvdecode')
    if not ffmpeg or not dec.is_file():return {}
    h,_=inspect_output(path);fps=f'{h.fps_num}/{h.fps_den}'
    flt=(f'[0:v]settb=AVTB,setpts=N*{h.fps_den}*1000000/{h.fps_num},split=2[d0][d1];'
         f'[1:v]settb=AVTB,setpts=N*{h.fps_den}*1000000/{h.fps_num},split=2[s0][s1];'
         '[d0][s0]psnr=shortest=1:repeatlast=0;[d1][s1]ssim=shortest=1:repeatlast=0')
    d=subprocess.Popen([str(dec),str(path)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    assert d.stdout is not None
    cmd=[ffmpeg,'-hide_banner','-nostats','-f','rawvideo','-pixel_format','yuv420p','-video_size',
         f'{h.width}x{h.height}','-framerate',fps,'-i','pipe:0','-i',str(source),
         '-filter_complex',flt,'-an','-f','null','-']
    q=subprocess.Popen(cmd,stdin=d.stdout,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    d.stdout.close();_,err=q.communicate();d.stderr.read();drc=d.wait()
    if drc or q.returncode:return {}
    txt=err.decode('utf-8','replace')
    pm=re.search(r'PSNR y:[^\r\n]* average:([0-9.]+|inf)',txt)
    sm=re.search(r'SSIM Y:[^\r\n]* All:([0-9.]+)',txt)
    return {'psnr_db':float(pm.group(1)) if pm and pm.group(1)!='inf' else None,
            'ssim':float(sm.group(1)) if sm else None}


def main():
    ap = argparse.ArgumentParser(description='Encode + verify a GHV file and print a repeatable benchmark report')
    ap.add_argument('input')
    ap.add_argument('output', nargs='?')
    ap.add_argument('--preset', choices=['veryfast','fast','compact','balanced','quality'], default='balanced')
    ap.add_argument('--codec', type=int, choices=[6, 7], default=7)
    ap.add_argument('--audio-quality', choices=['hq','compact'], default='hq')
    ap.add_argument('--threads', type=int, default=0)
    ap.add_argument('--decode-frames',type=int,default=180,help='decode benchmark frames; 0 = full file')
    ap.add_argument('--quality-metrics',action='store_true',help='measure full-file PSNR and SSIM')
    ap.add_argument('--report-json',help='also save the structured report to this path')
    ap.add_argument('--keep', action='store_true', help='keep auto-created output')
    ap.add_argument('--json', action='store_true')
    args = ap.parse_args()

    src = Path(args.input).resolve()
    if not src.is_file():
        raise SystemExit(f'input not found: {src}')
    temp_created = False
    if args.output:
        out = Path(args.output).resolve()
    else:
        fd, name = tempfile.mkstemp(prefix='ghvbench_', suffix='.ghv')
        os.close(fd); os.unlink(name)
        out = Path(name); temp_created = True

    cmd = [sys.executable, str(ROOT/'ghvenc.py'), str(src), str(out), '--preset', args.preset,
           '--audio-quality', args.audio_quality, '--codec', str(args.codec)]
    if args.threads > 0:
        cmd += ['--threads', str(args.threads)]

    t0 = time.perf_counter(); lines=[]
    p = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, encoding='utf-8', errors='replace', bufsize=1)
    assert p.stdout is not None
    for line in p.stdout:
        lines.append(line)
        if not args.json:
            # Windows legacy consoles may be GBK while a child emits UTF-8
            # replacement characters.  Encode explicitly so the benchmark
            # runner never aborts merely while relaying progress text.
            enc = sys.stdout.encoding or 'utf-8'
            sys.stdout.write(line.encode(enc, 'replace').decode(enc, 'replace'))
    rc = p.wait(); wall=time.perf_counter()-t0
    if rc != 0:
        raise SystemExit(rc)

    verify = subprocess.run([sys.executable, str(ROOT/'ghvverify.py'), str(out)], cwd=ROOT,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace')
    verified = verify.returncode == 0
    result = None
    for line in reversed(lines):
        m = RESULT_RE.search(line)
        if m:
            result = m; break
    in_size = src.stat().st_size; out_size = out.stat().st_size;h,actual_codec=inspect_output(out)
    report = {
        'input': str(src), 'output': str(out), 'preset': args.preset, 'codec': args.codec,
        'actual_codec': actual_codec, 'source': probe_source(src),
        'input_bytes': in_size, 'output_bytes': out_size,
        'size_ratio_output_over_input': (out_size / in_size) if in_size else None,
        'size_change_percent': ((out_size / in_size - 1.0) * 100.0) if in_size else None,
        'wall_seconds': wall, 'verified': verified,
        'output_bitrate_bps': (out_size*8/(h.duration_us/1e6)) if h.duration_us else None,
    }
    if result:
        report.update(frames=int(result.group(1)), duration_seconds=float(result.group(2)),
                      encoder_elapsed_seconds=float(result.group(4)), avg_fps=float(result.group(5)))
    report['decode_fps']=measure_decode(out,args.decode_frames)
    if args.quality_metrics:report.update(measure_quality(src,out))
    if args.report_json:
        rp=Path(args.report_json);rp.parent.mkdir(parents=True,exist_ok=True)
        rp.write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    if args.json:
        print(json.dumps(report, indent=2, ensure_ascii=False))
    else:
        print('\n=== GHV BENCHMARK ===')
        print(f'Input : {in_size/(1024*1024):.3f} MiB')
        print(f'Output: {out_size/(1024*1024):.3f} MiB')
        print(f'Ratio : {out_size/in_size:.3f}x input' if in_size else 'Ratio : n/a')
        print(f'Wall  : {wall:.3f}s')
        if result: print(f'Encode: {float(result.group(5)):.2f} FPS average')
        if report['decode_fps'] is not None: print(f'Decode: {report["decode_fps"]:.2f} FPS')
        if 'psnr_db' in report: print(f'Quality: PSNR {report["psnr_db"]:.3f} dB | SSIM {report["ssim"]:.6f}')
        print(f'Verify: {"PASS" if verified else "FAIL"}')
        if args.report_json: print(f'JSON  : {Path(args.report_json).resolve()}')
        if not verified: print(verify.stdout)

    if temp_created and not args.keep:
        try: out.unlink()
        except OSError: pass


if __name__ == '__main__':
    main()
