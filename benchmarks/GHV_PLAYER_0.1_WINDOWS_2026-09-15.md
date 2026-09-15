# GHV Player 0.1 Windows Acceptance — 2026-09-15

This milestone tests the native Win32/D3D11/WASAPI player, not the Python
developer player. The codec remains GHVC8 and no bitstream path changed.

## Build and architecture

- MSVC Release build: PASS.
- Portable-package launch using only packaged EXE/runtime DLLs: PASS.
- Video: D3D11 planar Y/U/V textures and shader conversion; no CPU RGB frame.
- Audio: shared-mode WASAPI at the encoded sample rate; device clock is master.
- Decode: `libghv` worker plus a resolution-aware bounded frame queue.
- Scheduling: early video waits, due video displays, superseded late video drops.
- Audio rate/pitch is never altered to compensate for video load.

The package smoke played a 10.067 s GHVC8 clip to EOF: 302/302 displayed,
zero drops, underruns, freezes, pitch/slowdown/speedup events, 18.5 ms maximum
scheduling drift, and exit code 0.

## Test B — 1920x1080, 30 fps, 104.118 s

| Run | Displayed | Dropped | Underrun | Freeze | Max drift | CPU* | Peak working set |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | 3121 | 0 | 0 | 0 | 19.0 ms | 31.09% | 167.6 MiB |
| 2 | 3121 | 0 | 0 | 0 | 18.3 ms | 31.05% | 167.8 MiB |
| 3 | 3121 | 0 | 0 | 0 | 19.0 ms | 30.76% | anomalous 1109.2 MiB |
| memory confirmation | 3121 | 0 | 0 | 0 | 19.3 ms | 30.65% | 165.4 MiB |

All three required runs had zero pitch, slowdown, or unexpected speedup events.
The third run's process peak counter reported 1.08 GiB once. A fourth complete
run plus external five-second working-set/private-byte sampling stayed bounded
at 165.4 MiB with no growth trend, so the isolated reading is retained here as
an anomaly rather than silently discarded.

## Test C — 3840x2160, 24 fps, 181.348 s

| Run | Displayed | Dropped | Underrun | Freeze | Max drift | CPU* | Peak working set |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | 4344 | 8 | 0 | 0 | 33.7 ms | 35.30% | 289.1 MiB |
| 2 | 4344 | 8 | 0 | 0 | 26.4 ms | 35.39% | 291.0 MiB |

Both required runs completed at normal audio rate with zero pitch, slowdown,
speedup, underrun, or freeze event. Eight late video frames were dropped in
each run. The four-frame 4K queue remained bounded and never starved; the drops
are presenter/scheduling lateness and are reported rather than hidden.

`CPU*` is average process CPU divided by all logical processors (whole-machine
capacity). GPU utilization was not captured by the current telemetry; D3D11
rendering was active.

## Threading experiment

Allowing all 20 OpenMP workers caused workers to spin between per-frame regions:
three Test B runs used 69.5–74.4% whole-machine CPU, and a 960x544 smoke used
82.0%. Setting passive OpenMP policy did not help. Capping the player decode
worker at eight OpenMP threads reduced the same smoke to 33.7% while retaining
a measured 57 fps 4K decoder microbenchmark and stable 4K24 playback. The
20-thread policy was rejected.

## Interaction verification scope

Automated native core tests passed prebuffer, presentation, middle-keyframe
seek, WASAPI open/play, pause clock freeze, seek/re-anchor/resume, and clean
shutdown on Tests B and C. Process smoke confirmed the GUI remained responsive
on both files. Open/file dialog, drag/drop, timeline dragging, resize,
aspect-fit, fullscreen auto-hide/restore, volume/mute, EOF/replay, and shortcuts
are implemented in the Win32 application.

The available automation surface in this environment explicitly exposed no
native-app control, so mouse-driven resize/fullscreen/drag-drop stress was not
robotically exercised. These interactions must not be described as manually or
automatically acceptance-tested here; they remain a focused human UI check for
the packaged build.

