# GHV Development Workflow

GitHub repository `GoouZi/GHV` is the permanent engineering record. Preserve
codec evolution, performance work, failed experiments, specifications, and
benchmark milestones; do not replace the repository with a new snapshot.

## Branch roles

- `main`: latest build-tested public beta milestone. Beta does not mean 1.0 or
  production-ready.
- `codex/ghvcN`: active work for a codec generation.
- `backup/*`: immutable recovery references with continuing long-term value.

Use a new codec-generation branch only for a real bitstream/architecture
generation. Pure performance changes stay on the current generation.

## Milestone loop

For each logical change:

1. Make one coherent implementation or documentation change.
2. Build every affected native/tool target.
3. Run the relevant unit/self/compatibility tests.
4. Benchmark the smallest representative fixture first.
5. Run Test A, then B, then C only when earlier gates show a real benefit.
6. Record regressions and rejected experiments honestly.
7. Update the specification when the bitstream changes.
8. Commit with one descriptive subject.
9. Push the commit to its active remote branch promptly.
10. Promote to `main` only after build, compatibility, quality, and benchmark
   gates pass.

Examples:

```text
GHVC9: add adaptive motion partition
GHAC2: add adaptive Rice residual coding
Studio: report codec versions dynamically
Benchmarks: record GHVC9 milestone 2
Docs: refresh README for beta release
```

Avoid vague subjects such as `update`, `fix stuff`, or `test123`. Do not squash
the project's historical development record or force-push shared branches.

## Version and release rules

- Update `VERSION.json`, then run `python tools/generate_version_header.py`.
- Run `python tests/test_version_sync.py` before committing a version change.
- Project beta releases use tags such as `v0.9.0-beta.1`.
- Codec milestones use tags such as `ghv-0.9-m1`.
- Never move a published recovery/stable tag.
- `CHANGELOG.md`, `PROJECT_STATE.md`, README files, specifications, and
  benchmark reports must agree with `VERSION.json` and verified results.

## Data and safety

Do not commit original A/B/C videos, generated multi-gigabyte GHV/GHA files,
PCM/WAV/YUV dumps, native build trees, packages, logs, credentials, tokens, or
local environment configuration. Commit small compatibility fixtures,
benchmark Markdown/JSON summaries, specifications, and useful verified visual
comparisons.

Before every public push:

```powershell
git status
git diff --check
git log --oneline --decorate -20
git push origin <branch>
```

Push tags explicitly only after verifying their target commits.
