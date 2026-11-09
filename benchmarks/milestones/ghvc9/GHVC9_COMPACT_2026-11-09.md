# GHVC9 compact profile, November 9, 2026

Full Windows A/B/C, Balanced q78, GHAC1 HQ. Enable both options on the
development encoder and use the matching native decoder, libghv or Player.

```powershell
ghvbench input.mp4 output.ghv --codec 9 --preset balanced --compact-motion --compact-coefficients --quality-metrics --decode-frames 0 --report-json report.json
```

| Source | M1 MiB | Compact MiB | Reduction | Encode fps | CRC decode fps | PSNR / SSIM |
|---|---:|---:|---:|---:|---:|---|
| A, 3822 frames | 210.616 | 177.811 | 15.58% | 158.180 | 856.518 | 45.355968 / 0.984213 |
| B, 3121 frames | 405.317 | 332.567 | 17.95% | 49.934 | 266.296 | 46.627935 / 0.990219 |
| C, 4352 frames | 3062.501 | 2424.346 | 20.84% | 10.427 | 52.478 | 47.084085 / 0.986903 |

The earlier motion + coefficient experiment produced 198.466 MiB on A.
Repacking the original I-frame coefficients alone took that to 191.573 MiB.
The final encoder also omits the unused zero code and uses an exponential
tail for larger coefficient levels. Frame types, timestamps and reconstructed
CRCs match the comparison files on all three clips. For A, SHA-256 of all
decoded YUV bytes from both M1 and this encoder was
`e6a78c57247c3ebb53b87e6e42e4e21282227dffa0e53e7951de27fde6de31d3`.

Full C decoded at 2.187 times real time. Single-run encode speeds on A/B
exceeded the frozen GHVC8 records of 155.223/48.795 fps. C fell 0.037 fps
below GHVC8's historical 10.464 fps. These are machine-specific measurements.
The next performance experiment should measure coefficient packing cost
against this commit on the same machine.

The Release build, CTest coefficient boundary and reconstruction tests,
GHVC4–9 self-tests, libghv indexed seek/audio and A index repair passed.
Raw summaries are in `reports/m2_compact_{A,B,C}_2026-11-09.json`.

## Native Player telemetry

One full run per file with the updated Windows Player, hidden window, muted
output, active WASAPI audio clock and automatic exit at EOF. B displayed all
3121 frames in 104.177 seconds, with zero drops and maximum A/V drift of
22.752 ms. C displayed 4345 of 4352 frames in 181.399 seconds, with seven
late drops and maximum drift of 19.920 ms. Both runs recorded zero video
underruns, freezes and audio speed events. M1's historical C run reported zero
drops. These runs measure the player's scheduling and audio clock. The hidden
window received no manual visual inspection.
