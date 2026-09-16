#!/usr/bin/env python3
from __future__ import annotations
import argparse, json, os, platform, re, shutil, struct, subprocess, sys, tempfile, threading, time
from pathlib import Path
from ghv.container import read_header, read_index, FRAME_FMT, FRAME_SIZE
from ghv.paths import PROJECT_ROOT, native_binary
from ghv.version import version_summary

ROOT = PROJECT_ROOT
RESULT_RE = re.compile(r'RESULT\s+frames=(\d+)\s+duration=([0-9.]+)\s+size_mib=([0-9.]+)\s+elapsed=([0-9.]+)\s+avg_fps=([0-9.]+)')
PROFILE_VALUE_RE = re.compile(r'([a-z_]+)=([0-9.eE+-]+)')


def parse_profile_line(lines, marker: str):
    line = next((x for x in reversed(lines) if marker in x), None)
    if not line:
        return None
    return {key: float(value) for key, value in PROFILE_VALUE_RE.findall(line)}


def system_info():
    info = {
        'cpu_model': os.environ.get('PROCESSOR_IDENTIFIER') or platform.processor() or 'unknown',
        'logical_cores': os.cpu_count(),
        'platform': platform.platform(),
    }
    if os.name == 'nt':
        try:
            cmd = ['powershell', '-NoProfile', '-Command',
                   'Get-CimInstance Win32_VideoController | Select-Object -ExpandProperty Name | ConvertTo-Json -Compress']
            raw = subprocess.check_output(cmd, text=True, encoding='utf-8', errors='replace', timeout=8).strip()
            gpu = json.loads(raw) if raw else []
            info['gpu_models'] = gpu if isinstance(gpu, list) else [gpu]
        except Exception:
            info['gpu_models'] = []
    return info


class PeakMemory:
    def __init__(self, pid: int):
        self.pid=pid;self.peak=0;self.stop=threading.Event();self.thread=None
    def start(self):
        try: import psutil
        except ImportError:return self
        def worker():
            root=psutil.Process(self.pid)
            while not self.stop.wait(.05):
                try:self.peak=max(self.peak,sum(p.memory_info().rss for p in [root]+root.children(recursive=True) if p.is_running()))
                except (psutil.NoSuchProcess,psutil.AccessDenied):pass
        self.thread=threading.Thread(target=worker,daemon=True);self.thread.start();return self
    def finish(self):
        self.stop.set()
        if self.thread:self.thread.join(timeout=1)
        return self.peak or None


def probe_source(path: Path):
    ffprobe=shutil.which('ffprobe')
    if not ffprobe:return {}
    cmd=[ffprobe,'-v','error','-show_entries',
         'stream=codec_type,width,height,avg_frame_rate,bit_rate,sample_rate,channels:format=duration,size,bit_rate','-of','json',str(path)]
    try:
        data=json.loads(subprocess.check_output(cmd,text=True,encoding='utf-8',errors='replace'))
        streams=data.get('streams') or [];v=next((s for s in streams if s.get('codec_type')=='video'),{});au=next((s for s in streams if s.get('codec_type')=='audio'),{});fmt=data.get('format') or {}
        return {'size_bytes':int(fmt.get('size') or path.stat().st_size),'width':int(v.get('width') or 0),'height':int(v.get('height') or 0),
                'fps':v.get('avg_frame_rate'),'duration_seconds':float(fmt.get('duration') or 0),
                'video_bitrate_bps':int(v.get('bit_rate') or 0),'container_bitrate_bps':int(fmt.get('bit_rate') or 0),
                'audio_sample_rate':int(au.get('sample_rate') or 0),'audio_channels':int(au.get('channels') or 0),
                'audio_bitrate_bps':int(au.get('bit_rate') or 0)}
    except Exception:return {}


def inspect_output(path: Path):
    with open(path,'rb') as f:
        h=read_header(f);idx=read_index(f,h);codec=0
        if idx:
            f.seek(idx[0][0]);rh=f.read(FRAME_SIZE)
            if len(rh)==FRAME_SIZE:codec=struct.unpack(FRAME_FMT,rh)[4]
    return h,codec


def measure_decode(path: Path,frames: int,profile: bool = False):
    dec=native_binary('ghvdecode')
    if not dec.is_file():return None
    h,_=inspect_output(path);n=h.frame_count if frames==0 else max(1,min(h.frame_count,frames))
    cmd=[str(dec),str(path),'--no-output','--verify','--frames',str(n)]
    if profile:cmd.append('--profile')
    p=subprocess.Popen(cmd,
                     stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace')
    mem=PeakMemory(p.pid).start();_,err=p.communicate();peak=mem.finish()
    if p.returncode:return None,peak,None
    m=re.search(r'fps=([0-9.]+)',err)
    return (float(m.group(1)) if m else None),peak,parse_profile_line(err.splitlines(),'GHV_DECODE_PROFILE')


def measure_quality(source: Path,path: Path):
    ffmpeg=shutil.which('ffmpeg');dec=native_binary('ghvdecode')
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
    ap.add_argument('--version', action='version', version=version_summary())
    ap.add_argument('input')
    ap.add_argument('output', nargs='?')
    ap.add_argument('--preset', choices=['veryfast','fast','compact','balanced','quality'], default='balanced')
    ap.add_argument('--codec', type=int, choices=[6, 7, 8, 9], default=9)
    ap.add_argument('--audio-quality', choices=['hq','compact'], default='hq')
    ap.add_argument('--threads', type=int, default=0)
    ap.add_argument('--decode-frames',type=int,default=180,help='decode benchmark frames; 0 = full file')
    ap.add_argument('--quality-metrics',action='store_true',help='measure full-file PSNR and SSIM')
    ap.add_argument('--report-json',help='also save the structured report to this path')
    ap.add_argument('--playback-runs',type=int,default=0,help='run controlled full playback N times and include telemetry')
    ap.add_argument('--profile',action='store_true',help='include native encode/decode stage timings')
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

    cmd = [sys.executable, '-m', 'ghv.cli.encode', str(src), str(out), '--preset', args.preset,
           '--audio-quality', args.audio_quality, '--codec', str(args.codec)]
    if args.threads > 0:
        cmd += ['--threads', str(args.threads)]
    if args.profile:
        cmd.append('--profile')

    t0 = time.perf_counter(); lines=[]
    p = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, encoding='utf-8', errors='replace', bufsize=1)
    encode_mem=PeakMemory(p.pid).start()
    assert p.stdout is not None
    for line in p.stdout:
        lines.append(line)
        if not args.json:
            # Windows legacy consoles may be GBK while a child emits UTF-8
            # replacement characters.  Encode explicitly so the benchmark
            # runner never aborts merely while relaying progress text.
            enc = sys.stdout.encoding or 'utf-8'
            sys.stdout.write(line.encode(enc, 'replace').decode(enc, 'replace'))
    rc = p.wait(); encode_peak=encode_mem.finish();wall=time.perf_counter()-t0
    if rc != 0:
        raise SystemExit(rc)

    verify = subprocess.run([sys.executable, '-m', 'ghv.cli.verify', str(out)], cwd=ROOT,
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
        'wall_seconds': wall, 'verified': verified, 'encode_peak_memory_bytes': encode_peak,
        'output_bitrate_bps': (out_size*8/(h.duration_us/1e6)) if h.duration_us else None,
        'system': system_info(),
    }
    if result:
        report.update(frames=int(result.group(1)), duration_seconds=float(result.group(2)),
                      encoder_elapsed_seconds=float(result.group(4)), avg_fps=float(result.group(5)))
    report['decode_fps'],report['decode_peak_memory_bytes'],decode_profile=measure_decode(out,args.decode_frames,args.profile)
    if args.profile:
        report['encode_profile']=parse_profile_line(lines,'GHV_ENCODE_PROFILE')
        report['decode_profile']=decode_profile
    if args.quality_metrics:report.update(measure_quality(src,out))
    if args.playback_runs>0:
        report['playback']=[]
        for run in range(1,args.playback_runs+1):
            stats=out.with_suffix(f'.playback-{run}.json')
            pp=subprocess.run([sys.executable,'-m','ghv.cli.play',str(out),'--engine','native','--stats',str(stats)],cwd=ROOT)
            if pp.returncode:report['playback'].append({'run':run,'passed':False,'exit_code':pp.returncode});continue
            data=json.loads(stats.read_text(encoding='utf-8'));data.pop('samples',None);data['run']=run;data['passed']=not data.get('freeze_events') and not data.get('slowdown_events') and not data.get('speedup_events') and not data.get('pitch_change_events');report['playback'].append(data)
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
        if encode_peak: print(f'Encode peak memory: {encode_peak/(1024*1024):.1f} MiB')
        if report['decode_fps'] is not None: print(f'Decode: {report["decode_fps"]:.2f} FPS')
        if report['decode_peak_memory_bytes']: print(f'Decode peak memory: {report["decode_peak_memory_bytes"]/(1024*1024):.1f} MiB')
        if 'psnr_db' in report: print(f'Quality: PSNR {report["psnr_db"]:.3f} dB | SSIM {report["ssim"]:.6f}')
        print(f'Verify: {"PASS" if verified else "FAIL"}')
        if args.report_json: print(f'JSON  : {Path(args.report_json).resolve()}')
        if not verified: print(verify.stdout)

    if temp_created and not args.keep:
        try: out.unlink()
        except OSError: pass


if __name__ == '__main__':
    main()
