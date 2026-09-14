# Source management

GHV is maintained as a real Git repository. Do not treat release ZIP files as the only source of truth.

## Current tags

- `v0.4` — first GHV/GHA rename + GHVC3 compression update
- `v0.5` — GHVC4 speed/stability update
- `v0.6` — GHVC6 HD pipeline update

Current development is on `codex/ghvc7`. Create `v0.7` only after review/merge
of the GHVC7 implementation and fixed-video benchmark evidence.

## Local workflow

```bash
git status
git log --oneline --decorate --graph --all
git tag
```

Before a new release:

```bash
git checkout main
# make changes + run tests
git add -A
git commit -m "GHV x.y: ..."
git tag vx.y
```

Generated native binaries under `native/bin/` are intentionally ignored by Git. They must be rebuilt from `native/*.cpp`.

## New conversation handoff

If development moves to another ChatGPT/Codex conversation, provide the latest source repository ZIP and ask the new session to read `PROJECT_STATE.md`, `README.md`, `CHANGELOG.md`, and the newest `SPEC_GHV_*.md` before editing.

## GitHub

The connected GitHub account currently did not expose an existing GHV repository when v0.5 was prepared. Once an empty repository such as `GoouZi/GHV` is created, this local repository can be pushed there and future releases can be committed/tagged remotely as well.
