# Restore the frozen GHVC8 stable build

Frozen on 2026-09-15 before native end-user player development.

- Stable commit: `8f8ec96e00468c9029c573e182520778be9d2f39`
- Stable annotated tag: `ghv-0.8-stable-perf2`
- Backup branch: `backup/ghvc8-stable-perf2`
- Git bundle: `GHV_GHVC8_STABLE_2026-09-15.bundle`
- Source snapshot: `GHV_GHVC8_STABLE_2026-09-15_SOURCE.zip`

The bundle and source snapshot are stored outside the repository in the sibling
`GHV/backups` directory. Large source videos, generated `.ghv` benchmark files,
native build directories, and Python caches are deliberately excluded from the
source ZIP. The repository has no submodules; all required codec source,
CMake/build scripts, specifications, tests, and documentation are included.

## Restore in the existing clone

To inspect or build the frozen version without changing the current branch:

```powershell
git switch --detach ghv-0.8-stable-perf2
cmake --build .\native\build --config Release
```

To create a new working branch at the frozen version:

```powershell
git switch -c restore/ghvc8-stable-perf2 ghv-0.8-stable-perf2
```

To move an intentionally disposable recovery branch back to the exact stable
tree, first verify that no wanted uncommitted work is present. The final command
discards changes on that recovery branch:

```powershell
git status
git switch restore/ghvc8-stable-perf2
git reset --hard ghv-0.8-stable-perf2
```

For individual files, prefer the non-destructive form below and review the diff
before committing:

```powershell
git restore --source ghv-0.8-stable-perf2 -- path\to\file
git diff -- path\to\file
```

## Restore from the bundle

```powershell
git bundle verify .\GHV_GHVC8_STABLE_2026-09-15.bundle
git clone .\GHV_GHVC8_STABLE_2026-09-15.bundle GHV_source_repo_restored
cd GHV_source_repo_restored
git switch -c restore/ghvc8-stable-perf2 ghv-0.8-stable-perf2
```

The annotated tag object is included in the bundle. Verify the recovered commit:

```powershell
git rev-parse ghv-0.8-stable-perf2^{}
git status --short
```

The expected commit is
`8f8ec96e00468c9029c573e182520778be9d2f39` and status must be clean.

## Restore from the source ZIP

Extract the ZIP into an empty directory, then configure and build normally:

```powershell
Expand-Archive .\GHV_GHVC8_STABLE_2026-09-15_SOURCE.zip .\GHV_source_snapshot
cd .\GHV_source_snapshot
cmake -S native -B native\build
cmake --build native\build --config Release
python tests\selftest_v08.py
```

The ZIP is a buildable source snapshot, not a Git repository. Use the bundle
when complete history, branches, tags, or exact Git recovery are required.
