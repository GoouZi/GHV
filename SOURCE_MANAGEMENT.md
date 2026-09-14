# Source management

GHV is now maintained as a normal Git project rather than only as release ZIP files.

Recommended branch: `main`

Current tags:

- `v0.4` — GHV 0.4 / GHA 0.2 baseline
- `v0.5` — speed, compression, playback stability update

## Local continuation

The source-repository ZIP distributed with 0.5 contains the `.git` directory. After extraction:

```bash
git log --oneline --decorate --graph --all
git status
git checkout v0.4
git checkout main
```

`PROJECT_STATE.md` is the handoff document for a new conversation or contributor. Read it before changing codec behavior.

## GitHub setup

Once an empty GitHub repository exists, add it once:

```bash
git remote add origin https://github.com/<owner>/<repo>.git
git push -u origin main --tags
```

After that, every release should update source first, run self-tests, commit, tag the release, and push the commit/tag.

Suggested release workflow:

```text
edit
 -> python tests/selftest_v05.py (or current-version equivalent)
 -> python ghvverify.py reference.ghv
 -> benchmark representative samples
 -> update CHANGELOG / PROJECT_STATE / SPEC when bitstream changes
 -> git commit
 -> git tag
 -> push
 -> build release ZIP
```

Do not commit generated benchmark outputs, caches, or local compiler binaries unless a release process explicitly needs them.
