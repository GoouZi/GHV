# GHVC9 encoder and Windows Player timing, November 10, 2026

The full-block path in motion RD now accumulates exactly 64 squared pixel
errors in a 32-bit sum. The maximum is 4,161,600. The inner loop uses row
pointers when both source and prediction fit, with the existing clamped path
for image boundaries. Quantization, candidate selection and the bitstream
syntax are unchanged.

## Encoder comparison

Test C's fixed 240-frame 4K24 excerpt, Windows Release build, Balanced,
automatic threads, native profiling enabled. GHVC9 used both compact options.
The pre-change executable encoded in order 8/9, 9/8, 8/9. After rebuilding,
GHVC9 encoded the same clip three times. Times include the media input path.

| Encoder | Run 1 fps | Run 2 fps | Run 3 fps | Median fps |
|---|---:|---:|---:|---:|
| GHVC8 before patch | 10.404 | 10.590 | 9.935 | 10.404 |
| GHVC9 before patch | 10.852 | 10.986 | 10.351 | 10.852 |
| GHVC9 after patch | 11.058 | 12.091 | 12.754 | 12.091 |

Median full-RD CPU time for GHVC9 fell from 104.934 to 94.901 seconds summed
across workers. Before/after clip files have identical SHA-256. A player build
overlapped part of the baseline sequence. The spread across runs matters for
interpreting small differences. The optimization is shared with GHVC8; the
GHVC8 row describes the pre-change reference.

Full C encoded in **369.660 seconds at 11.773 fps**, compared to 10.427 fps
on the previous compact run and 10.464 fps on frozen GHVC8 perf2. All
2,542,110,568 bytes match the previous compact C file by SHA-256. Full A
also matched the previous compact file exactly (177.811 MiB, 166.795 fps).

## Native playback

Three consecutive complete runs of the same 181.348-second compact Test C
file on the Windows Player. Window hidden, output muted, WASAPI audio clock
active. All three runs presented 4352 frames, with zero drops, underruns or
freezes. Maximum measured A/V drift was 25.986, 19.619 and 20.073 ms.

A separate control used the same instrumentation, decoder library, compiler
and OpenMP settings, with the old newest-due selection and Present on every
poll. It also presented all 4352 frames, with zero drops and 19.884 ms maximum
drift. No poll in these four runs saw two eligible frames. The earlier seven
drops have no per-frame trace. Their cause remains undetermined.

The updated renderer presents on new video or invalidation (including resize,
exposure and controls visibility). The player records PTS, playback clock,
time since enqueue and ready queue depth when two frames are eligible.
Slightly overlapping deadlines can be serviced on successive polls. Truly
late frames are still skipped to follow the audio clock. The native player
smoke test exercises both decisions with controlled clock inputs.

The [measurement JSON](reports/m2_4k_timing_2026-11-10.json) includes individual
runs and playback telemetry.
