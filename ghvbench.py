#!/usr/bin/env python3
from __future__ import annotations
import argparse, json, os, re, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
RESULT_RE = re.compile(r'RESULT\s+frames=(\d+)\s+duration=([0-9.]+)\s+size_mib=([0-9.]+)\s+elapsed=([0-9.]+)\s+avg_fps=([0-9.]+)')


def main():
    ap = argparse.ArgumentParser(description='Encode + verify a GHV file and print a repeatable benchmark report')
    ap.add_argument('input')
    ap.add_argument('output', nargs='?')
    ap.add_argument('--preset', choices=['veryfast','fast','balanced','quality'], default='balanced')
    ap.add_argument('--audio-quality', choices=['hq','compact'], default='hq')
    ap.add_argument('--threads', type=int, default=0)
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
           '--audio-quality', args.audio_quality]
    if args.threads > 0:
        cmd += ['--threads', str(args.threads)]

    t0 = time.perf_counter(); lines=[]
    p = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, encoding='utf-8', errors='replace', bufsize=1)
    assert p.stdout is not None
    for line in p.stdout:
        lines.append(line)
        if not args.json:
            print(line, end='')
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
    in_size = src.stat().st_size; out_size = out.stat().st_size
    report = {
        'input': str(src), 'output': str(out), 'preset': args.preset,
        'input_bytes': in_size, 'output_bytes': out_size,
        'size_ratio_output_over_input': (out_size / in_size) if in_size else None,
        'size_change_percent': ((out_size / in_size - 1.0) * 100.0) if in_size else None,
        'wall_seconds': wall, 'verified': verified,
    }
    if result:
        report.update(frames=int(result.group(1)), duration_seconds=float(result.group(2)),
                      encoder_elapsed_seconds=float(result.group(4)), avg_fps=float(result.group(5)))
    if args.json:
        print(json.dumps(report, indent=2, ensure_ascii=False))
    else:
        print('\n=== GHV BENCHMARK ===')
        print(f'Input : {in_size/(1024*1024):.3f} MiB')
        print(f'Output: {out_size/(1024*1024):.3f} MiB')
        print(f'Ratio : {out_size/in_size:.3f}x input' if in_size else 'Ratio : n/a')
        print(f'Wall  : {wall:.3f}s')
        if result: print(f'Encode: {float(result.group(5)):.2f} FPS average')
        print(f'Verify: {"PASS" if verified else "FAIL"}')
        if not verified: print(verify.stdout)

    if temp_created and not args.keep:
        try: out.unlink()
        except OSError: pass


if __name__ == '__main__':
    main()
