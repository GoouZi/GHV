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

The latest authoritative public result is
[`GHVC9_MILESTONE1_2026-09-16.md`](milestones/ghvc9/GHVC9_MILESTONE1_2026-09-16.md).

## Artifact policy

Commit Markdown reports, compact summary JSON, small compatibility fixtures,
and visual evidence needed to review a milestone. Do not commit source videos,
generated GHV/GHA media, YUV/PCM dumps, temporary profile output, or routine
per-frame telemetry. Exceptionally useful raw logs belong in a release or CI
artifact, not permanently in the current source tree.
