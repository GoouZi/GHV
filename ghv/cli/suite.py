"""Run ghvbench with reproducibility metadata and decoded-pixel hashes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

from ghv.container import read_header
from ghv.paths import PROJECT_ROOT, native_binary


def sha(path):
    with path.open('rb') as f:
        return digest(f)


def digest(f, expected=None):
    consumed = 0
    h = hashlib.sha256()
    while block := f.read(1 << 20):
        consumed += len(block)
        h.update(block)
    if expected is not None and consumed != expected:
        raise ValueError(f'expected {expected} decoded bytes, received {consumed}')
    return h.hexdigest()


def decode_cpu_seconds(proc):
    # Query the terminated child through its still-open Windows process handle.
    if os.name != 'nt':
        return None
    import ctypes
    from ctypes import wintypes
    fields = [wintypes.FILETIME() for _ in range(4)]
    if not ctypes.windll.kernel32.GetProcessTimes(
            wintypes.HANDLE(proc._handle), *(ctypes.byref(f) for f in fields)):
        return None
    return sum((f.dwHighDateTime << 32) + f.dwLowDateTime for f in fields[2:]) / 1e7


def run():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    p.add_argument('sources', type=Path, nargs='+')
    p.add_argument('--profile', choices=['compact', 'm1'], default='compact')
    p.add_argument('--threads', type=int, default=0)
    p.add_argument('--repeat', type=int, default=3)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--change', choices=['performance', 'compression', 'baseline'], required=True)
    a = p.parse_args()
    if a.threads < 0:
        p.error('thread count must be nonnegative')
    if a.repeat < 1 or len({s.stem for s in a.sources}) != len(a.sources):
        p.error('positive repeats and unique source stems required')
    dest = a.directory.resolve()
    dest.mkdir(parents=True, exist_ok=False)
    def capture(cmd):
        return subprocess.check_output(cmd, cwd=PROJECT_ROOT, text=True, encoding='utf8', errors='replace').strip()
    def save(path, data):
        path.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding='utf8')
    meta = dict(change=a.change, version=json.loads((PROJECT_ROOT/'VERSION.json').read_bytes()),
                git=capture(['git','rev-parse','HEAD']), status=capture(['git','status','--porcelain']),
                source_diff_sha256=hashlib.sha256(subprocess.check_output(['git','diff','HEAD','--','native','libghv'],cwd=PROJECT_ROOT)).hexdigest(),
                platform=platform.platform(), python=sys.version, ffmpeg=capture(['ffmpeg','-version']).splitlines()[0],
                environment={k:os.environ.get(k) for k in ('OMP_NUM_THREADS','OMP_DYNAMIC','OMP_WAIT_POLICY')},
                binaries={n:sha(native_binary(n)) for n in ('ghvcore','ghvdecode')})
    build = a.build.resolve()
    meta['cmake'] = [line for line in (build/'CMakeCache.txt').read_text(encoding='utf8').splitlines()
                     if line.startswith(('CMAKE_GENERATOR:', 'CMAKE_CXX_FLAGS', 'CMAKE_BUILD_TYPE:', 'OpenMP_CXX_FLAGS:'))]
    for proj in ('ghvcore','ghvdecode'):
        project = build/(proj+'.vcxproj')
        if project.exists():
            tree=ET.parse(project)
            meta[proj+'_release_compile']=[ET.tostring(n,encoding='unicode') for group in tree.findall('.//{*}ItemDefinitionGroup')
                    if 'Release|' in group.get('Condition','') for n in group.findall('{*}ClCompile')]
            meta['platform_toolset']=[n.text for n in tree.findall('.//{*}PlatformToolset')][0]
    compiler = sorted(build.glob('CMakeFiles/*/CMakeCXXCompiler.cmake'))
    if compiler:
        meta['compiler']=[x for x in compiler[-1].read_text(encoding='utf8').splitlines()
                          if x.startswith(('set(CMAKE_CXX_COMPILER_ID ', 'set(CMAKE_CXX_COMPILER_VERSION '))]
    save(dest/'environment.json',meta)
    for src in a.sources:
        source_sha=sha(src)
        runs=[]
        for i in range(a.repeat):
            name=f'{src.stem}-{i+1}'
            output=dest/(name+'.ghv')
            raw=dest/(name+'.json')
            cmd=[sys.executable,'-m','ghv.cli.benchmark',str(src.resolve()),str(output),'--codec','9','--preset',
                 'balanced','--audio-quality','hq','--threads',str(a.threads),'--quality-metrics',
                 '--decode-frames','0','--report-json',str(raw)]
            if a.profile == 'compact':
                cmd.extend(['--compact-motion','--compact-coefficients'])
            with (dest/(name+'.log')).open('w',encoding='utf8') as log:
                subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
            r=json.loads(raw.read_bytes())
            if not r['verified'] or not r['decode_fps'] or r.get('ssim') is None or r.get('psnr_db') is None:
                raise RuntimeError(f'incomplete verification: {raw}')
            with output.open('rb') as media:
                h=read_header(media)
            expected=h.frame_count*h.width*h.height*3//2
            start=time.perf_counter()
            with subprocess.Popen([native_binary('ghvdecode'),output,'--verify'],stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL) as proc:
                yuv=digest(proc.stdout,expected)
                if proc.wait(): raise RuntimeError('YUV decode failed')
                cpu=decode_cpu_seconds(proc)
            elapsed=time.perf_counter()-start
            r.update(source_sha256=source_sha, output_sha256=sha(output), yuv_sha256=yuv,
                     yuv_pipe_and_hash_seconds=elapsed, yuv_decode_cpu_seconds=cpu,
                     yuv_decode_core_equivalents=cpu/elapsed if cpu is not None else None, command=cmd[1:])
            save(raw,r)
            runs.append(r)
        if len({r['output_sha256'] for r in runs})!=1 or len({r['yuv_sha256'] for r in runs})!=1:
            raise RuntimeError('non-deterministic repeated output')
        save(dest/(src.stem+'-median.json'),{k:('inf' if runs[0][k]=='inf' else statistics.median(r[k] for r in runs))
             for k in ('wall_seconds','encoder_elapsed_seconds','avg_fps','decode_fps','decode_wall_seconds','output_bytes','ssim','psnr_db')})
if __name__=='__main__':
    run()
