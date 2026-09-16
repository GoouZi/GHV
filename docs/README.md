# GHV documentation

This directory contains the durable technical and development documentation for
GHV. The repository root is intentionally limited to the public project entry
points and executable tools.

## Specifications

- [GHV 0.9 / GHVC9](specs/ghv/0.9.md) — current video bitstream specification
- [GHV 0.8 / GHVC8](specs/ghv/0.8.md)
- [GHV 0.7 / GHVC7](specs/ghv/0.7.md)
- [Earlier GHV specifications](specs/ghv/)
- [GHA 0.2 / GHAC1](specs/gha/0.2.md)
- [Legacy GVID and GAUD specifications](specs/legacy/)

Historical specifications remain available because decoder compatibility is a
project requirement.

## Architecture and API

- [libghv API notes](api/libghv.md)
- [Player architecture](architecture/player.md)

## Development

- [Current handoff and project state](development/HANDOFF.md)
- [Development and Git workflow](development/WORKFLOW.md)
- [GHVC8 stable recovery procedure](development/recovery/GHVC8_STABLE.md)
- [Roadmap](ROADMAP.md)

## Integration and history

- [Godot integration notes](integrations/godot.md) — design notes; integration is deferred
- [GVID to GHV migration history](history/GVID_TO_GHV.md)

Measured codec and playback results are indexed separately in
[`benchmarks/README.md`](../benchmarks/README.md).
