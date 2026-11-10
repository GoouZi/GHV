"""Quality-matched FFmpeg references for fixed-frame-rate, 8-bit 4:2:0 suite sources."""
import argparse
from fractions import Fraction
import json
from pathlib import Path
import struct
import subprocess

from ghv.cli.benchmark import quality_filter, quality_summary
from ghv.cli.suite import sha
from ghv.container import FRAME_FMT, FRAME_SIZE, read_header, read_index


ENCODERS = {
    'h264': ('libx264', ['-preset', 'medium'], 51),
    'vp9': ('libvpx-vp9', ['-b:v', '0', '-cpu-used', '4'], 63),
    'av1': ('libsvtav1', ['-preset', '8'], 63),
}


def video_probe(path, entries, *options):
    result = subprocess.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0',
                                      *options, '-show_entries', entries, '-of', 'json', str(path)])
    return json.loads(result)


def quality(source, encoded, fps_num, fps_den):
    graph = quality_filter(fps_num, fps_den)
    cmd = ['ffmpeg', '-hide_banner', '-nostats', '-i', str(encoded), '-i', str(source),
           '-filter_complex', graph, '-an', '-fps_mode', 'passthrough', '-f', 'null', '-']
    metrics = quality_summary(subprocess.run(cmd, capture_output=True, check=True).stderr)
    if metrics['psnr_db'] is None or metrics['ssim'] is None:
        raise RuntimeError('FFmpeg quality metrics missing')
    return metrics['psnr_db'], metrics['ssim']


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('suite_run', type=Path, help='individual verified trial JSON')
    p.add_argument('directory', type=Path)
    p.add_argument('--encoders', nargs='+', choices=ENCODERS, default=list(ENCODERS))
    a = p.parse_args()
    anchor = json.loads(a.suite_run.read_bytes())
    media = a.suite_run.with_suffix('.ghv')
    if not anchor['verified'] or sha(a.source) != anchor['source_sha256'] or sha(media) != anchor['output_sha256']:
        p.error('source or anchor identity mismatch')
    source = video_probe(a.source, 'stream=width,height,pix_fmt,avg_frame_rate,nb_read_frames', '-count_frames')['streams'][0]
    with media.open('rb') as f:
        h = read_header(f)
        if (source['pix_fmt'] != 'yuv420p' or int(source['nb_read_frames']) != h.frame_count
                or (source['width'], source['height']) != (h.width, h.height)
                or Fraction(source['avg_frame_rate']) != Fraction(h.fps_num, h.fps_den)):
            p.error('comparison requires matching native frame count, rate, dimensions and yuv420p')
        video_bytes = 0
        for offset, kind in read_index(f, h):
            f.seek(offset)
            video_bytes += struct.unpack(FRAME_FMT, f.read(FRAME_SIZE))[-2]
    target = float(anchor['psnr_db'])
    if target == float('inf'):
        p.error('CRF search requires a finite PSNR target')
    a.directory.mkdir(parents=True, exist_ok=False)
    result = dict(source_sha256=anchor['source_sha256'], ghv_sha256=anchor['output_sha256'],
                  frames=h.frame_count, ghv_video_bytes=video_bytes, psnr_db=target, ssim=anchor['ssim'],
                  psnr_tolerance_db=0.5, ssim_tolerance=0.003, encoders={},
                  ffmpeg=subprocess.check_output(['ffmpeg','-version'],text=True).splitlines()[0])
    for name in a.encoders:
        encoder, options, maximum = ENCODERS[name]
        low, high = 0, maximum
        trials = []
        while low <= high:
            crf = (low + high) // 2
            output = a.directory / f'{name}-{crf}.mkv'
            cmd = ['ffmpeg', '-n', '-v', 'error', '-i', str(a.source), '-map', '0:v:0', '-an',
                   '-c:v', encoder, *options, '-crf', str(crf), '-pix_fmt', 'yuv420p',
                   '-fps_mode', 'passthrough', str(output)]
            subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
            packets = video_probe(output, 'packet=size:stream=nb_read_frames,extradata_size', '-count_frames')
            stream = packets['streams'][0]
            if int(stream['nb_read_frames']) != h.frame_count:
                raise RuntimeError('reference frame count mismatch')
            payload = sum(int(row['size']) for row in packets['packets']) + int(stream.get('extradata_size', 0))
            psnr, ssim = quality(a.source, output, h.fps_num, h.fps_den)
            trials.append(dict(crf=crf, psnr_db=psnr, ssim=ssim, video_bytes=payload, command=cmd))
            if float(psnr) >= target:
                low = crf + 1
            else:
                high = crf - 1
        matched = [t for t in trials if abs(float(t['psnr_db'])-target) <= 0.5 and abs(t['ssim']-anchor['ssim']) <= 0.003]
        result['encoders'][name] = dict(trials=trials, selected=min(matched, key=lambda t: abs(float(t['psnr_db'])-target)) if matched else None)
        (a.directory/'comparison.json').write_text(json.dumps(result, indent=2), encoding='utf8')


if __name__ == '__main__':
    main()
