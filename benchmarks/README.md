# GHV benchmarks

This directory stores compact, reviewable evidence for codec and playback
milestones. Generated media, temporary profiles, and per-frame telemetry are
local working data and are not part of the public source tree.

## Fixed test identities

- **Test A:** 960×544 at 30 fps — compression, quality, and visual comparison.
- **Test B:** 1920×1080 at 30 fps — HD throughput and playback stability.
- **Test C:** 3840×2160 at 24 fps — 4K throughput, memory, and playback stress.

These identities are stable across reports. Results from different tests,
presets, quality settings, or methodologies must not be mixed.

## Layout

- [`milestones/ghvc9/`](milestones/ghvc9/) — current canonical GHVC9 report,
  summary JSON, preset samples, and fixed Test A visual evidence.
- [`milestones/ghvc8/`](milestones/ghvc8/) — frozen GHVC8 codec and performance
  baselines.
- [`milestones/ghvc7/`](milestones/ghvc7/) — historical fixed-video milestone.
- [`player/`](player/) — native player acceptance report and compact run summaries.

The latest compact-profile [space audit and playback results](milestones/ghvc9/GHVC9_SPACE_2026-11-10.md) extend the original M1 baseline in
[`GHVC9_MILESTONE1_2026-09-16.md`](milestones/ghvc9/GHVC9_MILESTONE1_2026-09-16.md).

## Artifact policy

Commit Markdown reports, compact summary JSON, small compatibility fixtures,
and visual evidence needed to review a milestone. Do not commit source videos,
generated GHV/GHA media, YUV/PCM dumps, temporary profile output, or routine
per-frame telemetry. Exceptionally useful raw logs belong in a release or CI
artifact, not permanently in the current source tree.

## GHVC9 repeatable suite

Run from the checkout root with the project Python dependencies, CMake/C++17,
FFmpeg and ffprobe on PATH. Install psutil to collect sampled process-tree RSS.
Use a fresh build directory when changing toolchains or moving the checkout.

```powershell
cmake -S native -B native/build_bench -DCMAKE_BUILD_TYPE=Release
cmake --build native/build_bench --config Release
ctest --test-dir native/build_bench -C Release --output-on-failure
python -m ghv.cli.suite benchmarks/output/my-baseline "../测试视频/Test A.mp4" "../测试视频/Test B.mp4" "../测试视频/Test C.mp4" --build native/build_bench --repeat 3 --change baseline --profile compact
```

Use a new output directory for each experiment. Each source stem must be unique.
Use separate checkouts and their corresponding Release binaries for before/after
comparisons. Rebuild before running. The suite reads the supplied build metadata
and hashes the actual native/bin executables. Use a clean checkout for published
runs. Uncommitted and untracked source files are not recoverable from a hash.
Set equivalent power settings, background load and OpenMP environment in both legs.

* **Profiles.** Both use GHVC9 Balanced with HQ embedded audio and source timing.
  `--profile compact` enables compact motion and compact coefficients.
  `--profile m1` omits both. `--threads 0` selects automatic encoder threading.
  Use `--change performance` for speed-only work or `--change compression` for
  rate/quality changes. Compare reconstructed hashes in both cases.
* **Outputs.** Each trial writes the media, command log, metrics JSON, SHA-256 of
  source/media/decoded 8-bit planar YUV420 and sampled encode/decode peak RSS.
  Environment JSON records the commit, version, compiler/flags, binary hashes,
  platform and OpenMP overrides. Each source gets a separate median JSON.
  Repeats with different output or YUV hashes fail. Preserve failed run folders.
* **Timing.** Encoder elapsed/fps comes from the encoder, wall time includes CLI
  startup and audio. Decode fps and wall time are a full native CRC-verified
  decode to a null sink. The extra decode through a SHA-256 pipe is separately
  timed and byte-count checked. On Windows its child kernel+user CPU seconds
  divided by pipe wall time gives average occupied cores for that pass. This
  includes pipe backpressure. There is no comparable encode CPU measurement in
  this report. RSS is a 50 ms sample maximum, not an exact allocator peak.
* **Quality.** PSNR average and SSIM All use FFmpeg against the same source,
  paired by frame number. Use the fixed CFR fixtures for comparisons. PSNR
  is in dB. Full-container bitrate includes audio. Compare encoder-only rates
  using video payload bytes. VMAF is optional.

### Physical bit accounting

```powershell
python -m ghv.cli.account "benchmarks/output/my-baseline/Test A-1.ghv" benchmarks/output/a-space.json --frames benchmarks/output/a-frames.jsonl
```

The CMake build provides ghvbitaccount. The command opens existing media
read-only and refuses to overwrite report files. Every JSONL row includes the
index, type, PTS, frame offset, payload size, CRC, bit counts and, where
applicable, coefficient token counts. The aggregate `bits` dictionary is the
physical ledger. Frame type breakdowns are alternative views of the same bits.
Symbol counts and alternative level costs are diagnostics and must never be
added to the ledger. Unknown packet families are reported as opaque payloads.
GHAC1 audio uses its stored block size and residual width. GZP9 RLE controls
and stored values can span several logical fields in the expanded data.

### Optional quality-matched references

```powershell
python -m ghv.cli.reference "../测试视频/Test A.mp4" "benchmarks/output/my-baseline/Test A-1.json" benchmarks/output/my-reference
```

The verified run JSON and its adjacent .ghv must correspond to the source.
For fixed-rate native-resolution yuv420p inputs, the tool searches integer CRF
separately for libx264, libvpx-vp9 and libsvtav1. Select a subset with
`--encoders h264 vp9`. It retains every attempted setting, frame count, size
and measured PSNR/SSIM. A candidate must be within 0.5 dB and 0.003 SSIM of
the anchor to qualify; a null selection means no searched point qualified.
Sizes are GHV coded video payloads versus reference video packet bytes plus
codec extradata. This excludes embedded audio and container overhead on both
sides. A close metric pair on a short clip does not establish perceptual
equivalence, a BD-rate result, or a speed ranking.

### Native Windows player

Run the seek, prebuffer, deadline and WASAPI clock test first. For a complete
playback run, pass a fresh telemetry filename and wait for EOF. These commands
use the Release player and the actual test media generated above.

```powershell
$file = (Resolve-Path 'benchmarks/output/my-baseline/Test C-1.ghv').Path
& './native/bin/ghvplayertest.exe' 'benchmarks/output/my-baseline/Test C-1.ghv'
$stats = Join-Path (Resolve-Path 'benchmarks/output/my-baseline') 'C-hidden-1.json'
if (Test-Path -LiteralPath $stats) { throw "Use a new stats filename" }
$playerArgs = '"{0}" --stats "{1}" --exit-at-eof' -f $file, $stats
Start-Process './native/bin/GHV Player.exe' -ArgumentList $playerArgs -WindowStyle Hidden -Wait
Get-Content -LiteralPath $stats -Raw | ConvertFrom-Json
```

For a visible run, use a fresh stats path and invoke the player normally.
Record the window mode, audio device, CPU load, displayed/dropped frames,
underruns, freezes, maximum drift and the bounded due_events trace. The
November 10 hidden runs muted the player's output after opening the file and
retained WASAPI as master clock.
